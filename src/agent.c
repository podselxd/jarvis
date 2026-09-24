#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "app.h"
#include "config.h"
#include "groq.h"
#include "log.h"
#include "memory.h"
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
    bool announce_pending;
};

static cJSON *g_tools;
static char *g_system_prompt;

static const char *FAREWELLS[] = {
    "adios", "adiós", "hasta luego", "hasta la proxima", "hasta la próxima", "nos vemos", "me despido", "chao",
    "chau", "bye", "eso es todo", "eso seria todo", "eso sería todo", "nada mas", "nada más", "ya esta", "ya está",
    "ya no necesito nada", "gracias eso es todo",
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
static void trim_history(cJSON *h)
{
    int n = cJSON_GetArraySize(h);
    if (n <= MAX_HISTORY_MESSAGES + 1) return;
    int cut = n - MAX_HISTORY_MESSAGES;
    while (cut < n) {
        cJSON *role = cJSON_GetObjectItem(cJSON_GetArrayItem(h, cut), "role");
        if (cJSON_IsString(role) && !strcmp(role->valuestring, "user")) break;
        cut++;
    }
    for (int i = 1; i < cut; i++) cJSON_DeleteItemFromArray(h, 1);
}

Conversation *conv_create(bool load_recent_memory)
{
    Conversation *c = xcalloc(1, sizeof *c);
    c->history = cJSON_CreateArray();
    cJSON_AddItemToArray(c->history, system_message());
    if (load_recent_memory) {
        cJSON *recent = memory_recent_history(MEMORY_WINDOW_SECONDS);
        cJSON *m;
        while ((m = cJSON_DetachItemFromArray(recent, 0))) cJSON_AddItemToArray(c->history, m);
        cJSON_Delete(recent);
        trim_history(c->history);
    }
    c->announce_pending = true;
    return c;
}

void conv_destroy(Conversation *c)
{
    if (!c) return;
    cJSON_Delete(c->history);
    free(c);
}

void conv_new_session(Conversation *c)
{
    c->announce_pending = true;
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

static bool is_farewell(const char *text)
{
    char *low = str_lower(text);
    bool hit = false;
    for (size_t i = 0; i < sizeof FAREWELLS / sizeof *FAREWELLS && !hit; i++) hit = strstr(low, FAREWELLS[i]) != NULL;
    free(low);
    return hit;
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
        log_msg("Palabra de apagado detectada. Cerrando Jarvis.");
        r.reply = xstrdup("Jarvis desactivado.");
        r.shutdown = true;
        return r;
    }
    if (is_farewell(text)) {
        memory_persist("user", text);
        r.reply = xstrdup("Hasta luego.");
        return r;
    }

    int turn_start = cJSON_GetArraySize(c->history);
    cJSON *um = cJSON_CreateObject();
    cJSON_AddStringToObject(um, "role", "user");
    cJSON_AddStringToObject(um, "content", text);
    cJSON_AddItemToArray(c->history, um);
    memory_persist("user", text);

    char *reply = NULL;
    GroqError err = {0};
    bool failed = false;
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
            break;
        }
        cJSON_AddItemToArray(c->history, msg);
        cJSON *call;
        cJSON_ArrayForEach(call, calls)
        {
            cJSON *fn = cJSON_GetObjectItem(call, "function");
            const char *name = cJSON_GetStringValue(cJSON_GetObjectItem(fn, "name"));
            const char *args = cJSON_GetStringValue(cJSON_GetObjectItem(fn, "arguments"));
            const char *id = cJSON_GetStringValue(cJSON_GetObjectItem(call, "id"));
            char *result = run_tool(name ? name : "", args ? args : "{}");
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
    cJSON *am = cJSON_CreateObject();
    cJSON_AddStringToObject(am, "role", "assistant");
    cJSON_AddStringToObject(am, "content", reply);
    cJSON_AddItemToArray(c->history, am);
    shrink_tool_results(c->history, turn_start);
    memory_persist("assistant", reply);
    trim_history(c->history);
    r.reply = reply;
    r.keep_going = true;
    return r;
}
