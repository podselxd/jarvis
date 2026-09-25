#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "app.h"
#include "config.h"
#include "groq.h"
#include "intents.h"
#include "log.h"
#include "memory.h"
#include "mesh.h"
#include "resource.h"
#include "resources.h"
#include "tools.h"
#include "util.h"

#define MAX_HISTORY_MESSAGES 24
#define MEMORY_WINDOW_SECONDS (12 * 3600)
#define MAX_TOOL_ROUNDS 6
#define TOOL_RESULT_KEEP 280

struct Conversation {
    cJSON *history; /* [0] = system prompt, después user/assistant/tool */
    int session_start; /* desde aquí empieza la conversación actual ("Hey Sokari") */
    bool announce_pending;
    bool remote;        /* llega por la malla: ahí nadie puede confirmar de voz */
    bool trust_all;     /* dijo "sí a todo": no se vuelve a preguntar en esta conversación */
    char *pending_tool; /* acción que espera un "sí" de voz, con sus argumentos */
    char *pending_args;
};

static cJSON *g_tools;
static char *g_system_prompt;

static const char *FAREWELLS[] = {
    "adios", "adiós", "hasta luego", "hasta la proxima", "hasta la próxima", "nos vemos", "me despido", "chao",
    "chau", "bye", "eso es todo", "eso seria todo", "eso sería todo", "nada mas", "nada más", "ya esta", "ya está",
    "ya no necesito nada", "gracias eso es todo", "ya vete", "vete ya", "puedes irte", "te puedes ir",
    "ya nada gracias", "ya es todo", "retirate", "retírate",
};

/* Cuando Sokari mismo se despide, la conversación también termina (y la
   esfera se esconde si así está configurada). Una pregunta al final no cuenta:
   "¿algo más? si no, hasta luego" sigue esperando respuesta. */
static const char *SOKARI_FAREWELLS[] = {
    "hasta luego", "hasta pronto", "hasta la proxima", "hasta la próxima", "nos vemos", "adios", "adiós",
    "que tengas buen", "que tengas un buen", "que tengas lindo", "que tengas linda", "que tengas bonito",
    "que tengas excelente", "que descanses", "cuidate", "cuídate", "bye", "chao", "aqui estare", "aquí estaré",
    "quedo a la espera", "cuando me necesites",
};

void agent_init(void)
{
    g_tools = cJSON_Parse(res_string(IDR_TOOLS_JSON));
    g_system_prompt = res_string(IDR_SYSTEM_PROMPT);
    if (!g_tools) log_msg("No pude leer la definición de herramientas.");
}

static cJSON *system_message(void)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "system");
    cJSON_AddStringToObject(m, "content", g_system_prompt);
    return m;
}

/* Corta solo justo antes de un mensaje "user": un assistant con tool_calls
   tiene que quedar pegado a sus resultados "tool", si no Groq rechaza todos
   los turnos siguientes. */
static void trim_history(Conversation *c)
{
    cJSON *h = c->history;
    int n = cJSON_GetArraySize(h);
    if (n <= MAX_HISTORY_MESSAGES + 1) return;
    int cut = n - MAX_HISTORY_MESSAGES;
    while (cut < n) {
        cJSON *role = cJSON_GetObjectItem(cJSON_GetArrayItem(h, cut), "role");
        if (cJSON_IsString(role) && !strcmp(role->valuestring, "user")) break;
        cut++;
    }
    for (int i = 1; i < cut; i++) cJSON_DeleteItemFromArray(h, 1);
    c->session_start -= cut - 1;
    if (c->session_start < 1) c->session_start = 1;
}

Conversation *conv_create(bool load_recent_memory)
{
    Conversation *c = xcalloc(1, sizeof *c);
    c->history = cJSON_CreateArray();
    cJSON_AddItemToArray(c->history, system_message());
    c->session_start = 1;
    if (load_recent_memory) {
        cJSON *recent = memory_recent_history(MEMORY_WINDOW_SECONDS);
        cJSON *m;
        while ((m = cJSON_DetachItemFromArray(recent, 0))) cJSON_AddItemToArray(c->history, m);
        cJSON_Delete(recent);
        trim_history(c);
    }
    c->announce_pending = true;
    return c;
}

static void clear_pending(Conversation *c)
{
    free(c->pending_tool);
    free(c->pending_args);
    c->pending_tool = c->pending_args = NULL;
}

void conv_destroy(Conversation *c)
{
    if (!c) return;
    clear_pending(c);
    cJSON_Delete(c->history);
    free(c);
}

void conv_set_remote(Conversation *c, bool remote)
{
    c->remote = remote;
}

/* Lo que trajo una herramienta de afuera en una conversación anterior se
   borra: si traía instrucciones escondidas, ya no están para seguirlas. Queda
   la nota para que el historial siga siendo válido para Groq. */
static void forget_outside_text(cJSON *history, int until)
{
    for (int i = 1; i < until; i++) {
        const cJSON *call;
        cJSON_ArrayForEach(call, cJSON_GetObjectItem(cJSON_GetArrayItem(history, i), "tool_calls"))
        {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(call, "function"), "name"));
            const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(call, "id"));
            if (!id || !tool_brings_outside_text(name)) continue;
            for (int j = i + 1; j < until; j++) {
                cJSON *t = cJSON_GetArrayItem(history, j);
                const char *tid = cJSON_GetStringValue(cJSON_GetObjectItem(t, "tool_call_id"));
                cJSON *content = cJSON_GetObjectItem(t, "content");
                if (tid && !strcmp(tid, id) && cJSON_IsString(content))
                    cJSON_SetValuestring(content, "(texto de afuera de una conversación anterior; ya no está)");
            }
        }
    }
}

void conv_new_session(Conversation *c)
{
    c->announce_pending = true;
    c->trust_all = false;
    clear_pending(c);
    c->session_start = cJSON_GetArraySize(c->history);
    forget_outside_text(c->history, c->session_start);
}

/* Los resultados de herramientas (búsquedas, páginas, archivos) pueden ser
   largos; una vez que el modelo ya los resumió en su respuesta, se recortan en
   el historial para no volver a pagarlos en tokens en cada pedido siguiente. */
static void shrink_tool_results(cJSON *history, int from)
{
    int n = cJSON_GetArraySize(history);
    for (int i = from; i < n; i++) {
        cJSON *m = cJSON_GetArrayItem(history, i);
        cJSON *role = cJSON_GetObjectItem(m, "role");
        cJSON *content = cJSON_GetObjectItem(m, "content");
        if (!cJSON_IsString(role) || strcmp(role->valuestring, "tool") || !cJSON_IsString(content)) continue;
        size_t cut = utf8_truncate_len(content->valuestring, TOOL_RESULT_KEEP);
        if (content->valuestring[cut]) {
            char *shorter = str_printf("%.*s…", (int)cut, content->valuestring);
            cJSON_SetValuestring(content, shorter);
            free(shorter);
        }
    }
}

static void add_message(cJSON *history, const char *role, const char *content)
{
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", role);
    cJSON_AddStringToObject(m, "content", content);
    cJSON_AddItemToArray(history, m);
}

/* ¿Queda en la conversación algo que trajo una herramienta de afuera (una
   página, un archivo, el portapapeles...)? Mientras quede, el modelo puede
   estar siguiendo instrucciones escondidas ahí, también en un turno posterior:
   por eso no alcanza con mirar solo el turno en que se leyó. */
static bool history_has_outside_text(const Conversation *c)
{
    int n = cJSON_GetArraySize(c->history);
    for (int i = c->session_start; i < n; i++) {
        const cJSON *m = cJSON_GetArrayItem(c->history, i);
        const cJSON *call;
        cJSON_ArrayForEach(call, cJSON_GetObjectItem(m, "tool_calls"))
        {
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(call, "function"), "name"));
            if (tool_brings_outside_text(name)) return true;
        }
    }
    return false;
}

static bool word_in(const char *w, const char *const *list, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(w, list[i])) return true;
    return false;
}

/* Respuesta a "¿Lo hago?". Cualquier "no" en la frase gana ("claro que no",
   "sí, no, mejor no"). Un sí cuenta aunque venga acompañado ("ah ok, sí te lo
   doy", "te confirmo", "confirmo que sí"), pero solo si la frase no trae nada
   más: "sí, pero primero abre Spotify" se procesa como un pedido nuevo. */
AgentAnswer agent_classify_answer(const char *text)
{
    static const char *const YES[] = {"si",       "sí",        "dale",      "hazlo",   "hazle",      "confirmo",
                                      "confirma", "confirmado", "adelante", "claro",   "ok",         "okay",
                                      "okey",     "afirmativo", "correcto", "simón",   "simon",      "ándale",
                                      "andale",   "órale",      "orale",    "acuerdo", "autorizo",   "permiso"};
    static const char *const NO[] = {"no",      "cancela",  "cancelalo", "cancélalo", "olvidalo", "olvídalo",
                                     "nel",     "negativo", "nop",       "nope",      "tampoco",  "nunca"};
    /* Palabras que acompañan al sí sin cambiarlo. */
    static const char *const FILLER[] = {"ah",  "oh",  "eh",  "bueno", "pues", "que", "qué", "te",  "lo",
                                         "le",  "la",  "doy", "de",    "a",    "al",  "ya",  "y",   "por",
                                         "favor", "porfa", "sokari", "vale", "yo", "tienes", "mi"};
    static const char *const ALL[] = {"todo", "todos", "toda", "todas"};
    static const char *const REPEAT[] = {"que",         "qué",          "como",        "cómo",       "perdon",
                                         "perdón",      "mande",        "que dijiste", "qué dijiste", "repite",
                                         "repitelo",    "repítelo",     "otra vez",    "no te entendi",
                                         "no te entendí", "no entendi", "no entendí",  "cual",       "cuál",
                                         "que cosa",    "qué cosa",     "como dices",  "cómo dices"};
    char *low = str_lower(text);
    for (unsigned char *p = (unsigned char *)low; *p; p++) {
        if (p[0] == 0xC2 && (p[1] == 0xA1 || p[1] == 0xBF)) p[0] = p[1] = ' '; /* ¡ ¿ */
        else if (*p < 0x80 && !isalnum(*p)) *p = ' ';
    }
    str_collapse_spaces(low);
    char *t = str_trim(low);
    free(low);
    if (word_in(t, REPEAT, sizeof REPEAT / sizeof *REPEAT)) {
        free(t);
        return ANSWER_REPEAT;
    }
    int words = 0;
    bool any_no = false, any_yes = false, any_all = false, only_known = true;
    for (char *w = t; *w;) {
        char *end = strchr(w, ' ');
        if (end) *end = 0;
        words++;
        bool yes = word_in(w, YES, sizeof YES / sizeof *YES), all = word_in(w, ALL, sizeof ALL / sizeof *ALL);
        if (word_in(w, NO, sizeof NO / sizeof *NO)) any_no = true;
        any_yes |= yes;
        any_all |= all;
        if (!yes && !all && !word_in(w, FILLER, sizeof FILLER / sizeof *FILLER)) only_known = false;
        if (!end) break;
        w = end + 1;
    }
    free(t);
    if (any_no) return ANSWER_NO;
    if (!words || !only_known || words > 8) return ANSWER_OTHER;
    if (any_all) return ANSWER_ALL; /* "a todo", "sí a todo", "confirma todo", "permiso a todo" */
    return any_yes ? ANSWER_YES : ANSWER_OTHER;
}

/* "¿Qué?" con una acción pendiente: se vuelve a decir cuál es, sin
   cancelarla. */
static TurnResult repeat_pending(Conversation *c, const char *text)
{
    TurnResult r = {0};
    cJSON *parsed = cJSON_Parse(c->pending_args);
    if (!cJSON_IsObject(parsed)) {
        cJSON_Delete(parsed);
        parsed = cJSON_CreateObject();
    }
    char *desc = tool_describe_action(c->pending_tool, parsed);
    cJSON_Delete(parsed);
    r.reply = str_printf("Te preguntaba si puedo %s. ¿Lo hago? Di sí o no.", desc);
    free(desc);
    add_message(c->history, "user", text);
    memory_persist("user", text);
    add_message(c->history, "assistant", r.reply);
    memory_persist("assistant", r.reply);
    trim_history(c);
    r.keep_going = true;
    return r;
}

/* El "sí" ejecuta exactamente la acción guardada, sin volver a preguntarle
   al modelo. */
static TurnResult run_pending(Conversation *c, const char *text)
{
    TurnResult r = {0};
    log_msg("[confirmada] %s(%s)", c->pending_tool, c->pending_args);
    char *result = run_tool(c->pending_tool, c->pending_args);
    clear_pending(c);
    add_message(c->history, "user", text);
    memory_persist("user", text);
    add_message(c->history, "assistant", result);
    memory_persist("assistant", result);
    trim_history(c);
    r.reply = result;
    r.keep_going = true;
    return r;
}

static bool contains_stop_word(const char *text)
{
    char *stop = config_stop_word();
    char *t = str_trim(stop);
    bool hit = *t && str_contains_ci(text, t);
    SecureZeroMemory(stop, strlen(stop));
    free(stop);
    free(t);
    return hit;
}

static bool contains_any(const char *text, const char *const *list, size_t n)
{
    char *low = str_lower(text);
    bool hit = false;
    for (size_t i = 0; i < n && !hit; i++) hit = strstr(low, list[i]) != NULL;
    free(low);
    return hit;
}

static bool is_farewell(const char *text)
{
    return contains_any(text, FAREWELLS, sizeof FAREWELLS / sizeof *FAREWELLS);
}

static bool sokari_says_goodbye(const char *reply)
{
    char *t = str_trim(reply);
    size_t n = strlen(t);
    bool question = n && t[n - 1] == '?';
    free(t);
    return !question && contains_any(reply, SOKARI_FAREWELLS, sizeof SOKARI_FAREWELLS / sizeof *SOKARI_FAREWELLS);
}

/* A veces el modelo se traba y devuelve cientos de renglones de puntos y
   rayas. Eso no se dice en voz alta ni se guarda: casi no trae letras. */
static bool reply_is_garbage(const char *text)
{
    int letters = 0, other = 0, lines = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p;) {
        int len = *p >= 0xF0 ? 4 : *p >= 0xE0 ? 3 : *p >= 0xC0 ? 2 : 1;
        for (int k = 1; k < len; k++)
            if (!p[k]) {
                len = k;
                break;
            }
        unsigned cp = *p;
        if (len == 2) cp = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
        else if (len == 3) cp = ((p[0] & 0x0Fu) << 12) | ((p[1] & 0x3Fu) << 6) | (p[2] & 0x3Fu);
        else if (len == 4) cp = 0x10000; /* emojis y demás: no son letras */
        if (cp == '\n') lines++;
        if (cp < 0x80 ? isalnum((int)cp) : (cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7)) letters++;
        else if (cp > ' ') other++;
        p += len;
    }
    return lines > 40 || (letters + other >= 20 && letters * 2 < letters + other);
}

/* Borrar siempre pide un sí de voz, con o sin acceso completo. */
static bool tool_is_delete(const char *name)
{
    return !strcmp(name, "borrar_archivo");
}

/* Con acceso completo solo se pregunta antes de borrar. Sin él, como antes:
   las acciones delicadas, cuando en la conversación hay texto de afuera. */
static bool must_confirm(const Conversation *c, const char *name, const cJSON *args)
{
    if (c->trust_all) return false;
    if (tool_is_delete(name)) return true;
    return !config_full_access() && tool_needs_confirmation(name, args) && history_has_outside_text(c);
}

/* ¿La respuesta termina pidiendo permiso para hacer algo ("¿Quieres que lo
   mueva?", "¿Lo envío?")? No cuentan las que piden un dato (¿a quién?, ¿cuál?),
   las que dan a elegir (¿esto o aquello?) ni las que ofrecen algo más. */
bool agent_asks_permission(const char *reply)
{
    char *t = str_trim(reply);
    size_t n = strlen(t);
    bool ends_q = n && t[n - 1] == '?';
    const char *q = NULL;
    for (const char *p = strstr(t, "¿"); p; p = strstr(p + 1, "¿")) q = p;
    if (!q) {
        q = t;
        for (const char *p = t; *p; p++)
            if ((*p == '.' || *p == '!' || *p == '\n') && p[1]) q = p + 1;
    }
    char *low = str_lower(q);
    static const char *const ASK[] = {
        "quieres que",   "deseas que",     "te gustaría que", "te parece si",   "te parece que",  "prefieres que lo",
        "lo hago",       "procedo",        "confirmas",       "estás seguro",   "estas seguro",   "seguro que quieres",
        "lo envío",      "lo envio",       "lo mando",        "lo guardo",      "lo recuerdo",    "lo exporto",
        "lo muevo",      "lo abro",        "lo cierro",       "lo subo",        "lo publico",     "lo intento",
        "te lo mando",   "te lo envío",    "sigo",            "continúo",       "continuo",       "puedo",
        "me permites",   "me das permiso", "autorizas",       "está bien si",   "esta bien si",   "le doy",
    };
    static const char *const NOT[] = {"qué",     "cuál",    "cuáles",   "quién",     "quiénes",   "dónde",
                                      "cuándo",  "cómo",    "cuánto",   "cuánta",    "cuántos",   "cuántas",
                                      " o ",     "algo más", "otra cosa", "en qué más", "más ayuda", "ayudarte con"};
    bool ask = false;
    for (size_t i = 0; i < sizeof ASK / sizeof *ASK && !ask; i++) ask = strstr(low, ASK[i]) != NULL;
    for (size_t i = 0; i < sizeof NOT / sizeof *NOT && ask; i++) ask = strstr(low, NOT[i]) == NULL;
    free(low);
    free(t);
    return ends_q && ask;
}

/* La fecha se inyecta fresca en cada pedido (no queda en el historial) para
   que los recordatorios relativos ("en 10 minutos") se calculen bien. */
static cJSON *context_message(Conversation *c)
{
    StrBuf sb;
    sb_init(&sb);
    char *now = local_iso_now();
    sb_appendf(&sb, "Fecha y hora actual: %s", now);
    free(now);
    if (strcmp(current_speaker(), DEFAULT_PROFILE)) {
        char *name = profile_display_name(current_speaker());
        sb_appendf(&sb, "\nEstás hablando con: %s.", name);
        free(name);
    }
    /* Los nombres exactos de tus PCs: así "dile a mi laptop" llega a la que es. */
    MeshDevice *devs;
    int nd = mesh_devices(&devs);
    for (int i = 0; i < nd; i++)
        sb_appendf(&sb, "%s%s", i ? ", " : "\nDispositivos registrados para gestionar_dispositivo: ", devs[i].name);
    if (nd) sb_append(&sb, ".");
    mesh_devices_free(devs, nd);
    /* Va en cada pedido: si cambia el modo, cuenta desde el siguiente. */
    if (config_full_access())
        sb_append(&sb, "\nTienes acceso completo: quien habla te dio permiso para todo. Nunca pidas permiso ni "
                       "confirmación ('¿lo hago?', '¿quieres que...?'): hazlo directo y di en pocas palabras qué "
                       "hiciste. Guarda sin preguntar los datos personales que te cuente, exporta a Obsidian sin "
                       "preguntar y manda mensajes con type_text (enviar=true) sin preguntar. Solo borrar necesita un "
                       "sí, y ese lo pide el sistema.");
    else
        sb_append(&sb, "\nPide permiso solo en estos casos: si te cuenta o corrige un dato personal, pregunta si "
                       "quiere que lo recuerdes (si ya te lo pidió, guárdalo directo); antes de exportar_a_obsidian, "
                       "pregunta y espera un sí; si type_text le llega a otra persona (mensaje, email, publicación), "
                       "deja enviar=false y pregunta antes, salvo que ya te lo haya pedido. Fuera de esos casos no "
                       "pidas permiso: hazlo; si una acción necesita confirmación, el sistema la pide solo.");
    if (c->announce_pending) {
        char *pend = reminders_take_pending_for(current_speaker());
        if (*pend)
            sb_appendf(&sb, "\nEsta persona te pidió que le recordaras esto (menciónaselo en esta respuesta): %s.",
                       pend);
        free(pend);
        c->announce_pending = false;
    }
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "role", "system");
    cJSON_AddStringToObject(m, "content", sb.data);
    sb_free(&sb);
    return m;
}

static cJSON *build_request(Conversation *c)
{
    cJSON *msgs = cJSON_CreateArray();
    int n = cJSON_GetArraySize(c->history);
    cJSON_AddItemToArray(msgs, cJSON_Duplicate(cJSON_GetArrayItem(c->history, 0), 1));
    cJSON_AddItemToArray(msgs, context_message(c));
    for (int i = 1; i < n; i++) cJSON_AddItemToArray(msgs, cJSON_Duplicate(cJSON_GetArrayItem(c->history, i), 1));
    return msgs;
}

static char *error_reply(const GroqError *e)
{
    switch (e->status) {
    case GROQ_RATE_LIMITED:
        if (e->retry_after > 0 && e->retry_after < 120)
            return str_printf("Me quedé sin cupo de peticiones por ahora; dame unos %d segundos.", e->retry_after);
        return xstrdup("Me quedé sin cupo de peticiones por ahora, dame un momento.");
    case GROQ_AUTH_ERROR:
        return xstrdup("Tu API key de Groq no es válida o expiró. Revísala en Configuración.");
    case GROQ_NETWORK_ERROR:
        return xstrdup("No puedo conectar con Groq ahora mismo, ¿hay internet?");
    default:
        return xstrdup("No puedo conectar con Groq ahora mismo.");
    }
}

TurnResult agent_process(Conversation *c, const char *text)
{
    TurnResult r = {0};
    if (contains_stop_word(text)) {
        clear_pending(c);
        log_msg("Palabra de apagado detectada. Cerrando Sokari.");
        r.reply = xstrdup("Sokari desactivado.");
        r.shutdown = true;
        return r;
    }
    if (c->pending_tool) {
        AgentAnswer ans = agent_classify_answer(text);
        if (ans == ANSWER_REPEAT) return repeat_pending(c, text);
        if (ans == ANSWER_ALL) {
            c->trust_all = true;
            log_msg("Sí a todo: en esta conversación ya no pido confirmación.");
        }
        if (ans == ANSWER_YES || ans == ANSWER_ALL) return run_pending(c, text);
        clear_pending(c);
        if (ans == ANSWER_NO) {
            add_message(c->history, "user", text);
            memory_persist("user", text);
            r.reply = xstrdup("Va, no lo hago.");
            add_message(c->history, "assistant", r.reply);
            memory_persist("assistant", r.reply);
            r.keep_going = true;
            return r;
        }
        /* Otra cosa: la acción se descarta y el texto se procesa como pedido nuevo. */
    }
    if (is_farewell(text)) {
        memory_persist("user", text);
        r.reply = xstrdup("Hasta luego.");
        return r;
    }
    /* Play, pausa, volumen, la ventana de enfrente, abrir una app o una
       carpeta, "gracias": se hacen aquí, sin gastar cupo ni arriesgarse a
       que el modelo diga "listo" sin hacerlo. */
    IntentList il;
    if (intents_parse(text, &il)) {
        bool handled = false;
        char *done = intents_run(&il, &handled);
        if (handled) {
            log_msg("Comando directo, sin IA: «%s» -> %s", text, done);
            add_message(c->history, "user", text);
            memory_persist("user", text);
            add_message(c->history, "assistant", done);
            memory_persist("assistant", done);
            trim_history(c);
            r.reply = done;
            /* Un "gracias" solo es despedirse: igual que cuando Sokari se despide. */
            r.keep_going = !(il.n == 1 && il.items[0].kind == IN_THANKS);
            return r;
        }
        free(done);
    }

    int turn_start = cJSON_GetArraySize(c->history);
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", text);
    cJSON_AddItemToArray(c->history, um);
    memory_persist("user", text);

    char *reply = NULL;
    GroqError err = {0};
    bool failed = false, ending = false, auto_yes = false;
    for (int round = 0; round < MAX_TOOL_ROUNDS && !reply && !failed; round++) {
        app_status(round ? "Trabajando…" : "Pensando…");
        cJSON *msgs = build_request(c);
        cJSON *msg = groq_chat(msgs, g_tools, &err);
        cJSON_Delete(msgs);
        if (!msg) {
            failed = true;
            break;
        }
        cJSON *calls = cJSON_GetObjectItem(msg, "tool_calls");
        if (!cJSON_IsArray(calls) || !cJSON_GetArraySize(calls)) {
            cJSON *content = cJSON_GetObjectItem(msg, "content");
            reply = str_trim(cJSON_IsString(content) ? content->valuestring : "");
            cJSON_Delete(msg);
            /* Con acceso completo, si aun así pregunta "¿lo hago?", se le
               contesta que sí (una vez por turno) en vez de hacerte contestar. */
            if (!auto_yes && config_full_access() && round + 1 < MAX_TOOL_ROUNDS && agent_asks_permission(reply)) {
                auto_yes = true;
                log_msg("Acceso completo: el modelo preguntó «%s» y le contesto que sí.", reply);
                add_message(c->history, "assistant", reply);
                add_message(c->history, "user", "Sí, hazlo. Tienes acceso completo: no vuelvas a preguntar.");
                free(reply);
                reply = NULL;
                continue;
            }
            break;
        }
        cJSON_AddItemToArray(c->history, msg);
        const char *said = cJSON_GetStringValue(cJSON_GetObjectItem(msg, "content"));
        cJSON *call;
        bool blocked = false;
        cJSON_ArrayForEach(call, calls)
        {
            cJSON *fn = cJSON_GetObjectItem(call, "function");
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(fn, "name"));
            const char *args = cJSON_GetStringValue(cJSON_GetObjectItem(fn, "arguments"));
            const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(call, "id"));
            if (!name) name = "";
            if (!args) args = "{}";
            char *result;
            cJSON *parsed = cJSON_Parse(args);
            if (!cJSON_IsObject(parsed)) {
                cJSON_Delete(parsed);
                parsed = cJSON_CreateObject();
            }
            if (blocked) {
                /* cada tool_call necesita su resultado o Groq rechaza el historial */
                result = xstrdup("No se hizo: primero hay que confirmar la acción anterior.");
            } else if (!strcmp(name, "terminar_conversacion")) {
                ending = true;
                result = xstrdup("Listo: la conversación termina con esta respuesta.");
            } else if (must_confirm(c, name, parsed)) {
                blocked = true;
                char *desc = tool_describe_action(name, parsed);
                log_msg("[confirmación] %s(%s) espera un sí de voz", name, args);
                if (c->remote) {
                    result = xstrdup("No se hizo: necesita confirmación de voz en esa PC.");
                    reply = str_printf("Eso necesita que alguien lo confirme de voz en esta PC, así que no lo hice: %s.",
                                       desc);
                } else {
                    c->pending_tool = xstrdup(name);
                    c->pending_args = xstrdup(args);
                    result = xstrdup("Pendiente: se le pidió confirmación de voz a quien habla.");
                    reply = str_printf("%s: %s. ¿Lo hago? Di sí o no.",
                                       tool_is_delete(name) ? "Antes de borrar siempre te pregunto"
                                                            : "Como en esta conversación leí algo de afuera, "
                                                              "confirma primero",
                                       desc);
                }
                free(desc);
            } else {
                result = run_tool(name, args);
            }
            cJSON_Delete(parsed);
            char *shown = xstrndup(result, utf8_truncate_len(result, 300));
            log_msg("[herramienta] %s(%s) -> %s", name ? name : "?", args ? args : "", shown);
            free(shown);
            cJSON *tm = cJSON_CreateObject();
            cJSON_AddStringToObject(tm, "role", "tool");
            cJSON_AddStringToObject(tm, "tool_call_id", id ? id : "");
            cJSON_AddStringToObject(tm, "content", result);
            cJSON_AddItemToArray(c->history, tm);
            free(result);
        }
        /* Despedirse no necesita otra vuelta al modelo: se usa lo que dijo junto
           con la herramienta, o un "hasta luego". */
        if (ending && !reply) reply = str_trim(said && *said ? said : "Hasta luego.");
    }
    app_status("");

    if (failed) {
        while (cJSON_GetArraySize(c->history) > turn_start) cJSON_DeleteItemFromArray(c->history, turn_start);
        r.reply = error_reply(&err);
        r.keep_going = true;
        groq_error_free(&err);
        return r;
    }
    groq_error_free(&err);
    if (!reply) reply = xstrdup("Me hice bolas con eso, prueba de nuevo.");
    if (!*reply) {
        free(reply);
        reply = xstrdup("Listo.");
    }
    if (reply_is_garbage(reply)) {
        log_msg("El modelo respondió algo sin sentido (casi sin letras); lo descarto.");
        free(reply);
        reply = xstrdup("Me trabé con esa respuesta. ¿Me lo repites?");
    }
    cJSON *am = cJSON_CreateObject();
    cJSON_AddStringToObject(am, "role", "assistant");
    cJSON_AddStringToObject(am, "content", reply);
    cJSON_AddItemToArray(c->history, am);
    shrink_tool_results(c->history, turn_start);
    memory_persist("assistant", reply);
    trim_history(c);
    r.reply = reply;
    /* Si quedó una pregunta de confirmación, la conversación sigue aunque la
       respuesta suene a despedida: hay que poder contestarla. */
    r.keep_going = c->pending_tool || (!ending && !sokari_says_goodbye(reply));
    return r;
}
