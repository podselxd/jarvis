#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intents.h"
#include "keys.h"
#include "log.h"
#include "tools.h"
#include "util.h"

/* ------------------------------------------------------------- texto --- */

static int utf8_len(unsigned char c)
{
    return c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
}

char *intents_normalize(const char *text)
{
    StrBuf sb;
    sb_init(&sb);
    sb_append_char(&sb, ' ');
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        char c = ' ';
        int len = 1;
        if (*p < 0x80) {
            if (isalnum(*p)) c = (char)tolower(*p);
        } else if (*p == 0xC3 && p[1]) {
            len = 2;
            switch (p[1]) {
            case 0xA1: case 0x81: c = 'a'; break; /* á Á */
            case 0xA9: case 0x89: c = 'e'; break; /* é É */
            case 0xAD: case 0x8D: c = 'i'; break; /* í Í */
            case 0xB3: case 0x93: c = 'o'; break; /* ó Ó */
            case 0xBA: case 0x9A: case 0xBC: case 0x9C: c = 'u'; break; /* ú Ú ü Ü */
            case 0xB1: case 0x91: c = 'n'; break; /* ñ Ñ */
            default: break;
            }
        } else {
            len = utf8_len(*p);
            for (int i = 1; i < len; i++)
                if (!p[i]) len = i;
        }
        if (c != ' ') sb_append_char(&sb, c);
        else if (sb.data[sb.len - 1] != ' ') sb_append_char(&sb, ' ');
        p += len;
    }
    if (sb.data[sb.len - 1] != ' ') sb_append_char(&sb, ' ');
    return sb.data;
}

/* Clave para comparar cómo suena: z→s, c/q→k, y→i, sin h, sin letras dobles. */
static void sound_key(const char *w, char *out, size_t cap)
{
    size_t n = 0;
    for (const char *p = w; *p && n + 1 < cap; p++) {
        char c = *p == 'z' ? 's' : (*p == 'c' || *p == 'q') ? 'k' : *p == 'y' ? 'i' : *p;
        if (c == 'h') continue;
        if (n && out[n - 1] == c) continue;
        out[n++] = c;
    }
    out[n] = 0;
}

static int edit_distance(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    if (la > 15 || lb > 15) return 99;
    int d[16][16];
    for (size_t i = 0; i <= la; i++) d[i][0] = (int)i;
    for (size_t j = 0; j <= lb; j++) d[0][j] = (int)j;
    for (size_t i = 1; i <= la; i++)
        for (size_t j = 1; j <= lb; j++) {
            int sub = d[i - 1][j - 1] + (a[i - 1] != b[j - 1]);
            int del = d[i - 1][j] + 1, ins = d[i][j - 1] + 1;
            d[i][j] = sub < del ? (sub < ins ? sub : ins) : (del < ins ? del : ins);
        }
    return d[la][lb];
}

bool intents_sounds_like(const char *w, const char *target)
{
    if (!strcmp(w, target)) return true;
    char a[32], b[32];
    sound_key(w, a, sizeof a);
    sound_key(target, b, sizeof b);
    size_t n = strlen(b);
    if (n < 3 || strlen(a) < 3) return false;
    return edit_distance(a, b) <= (n <= 4 ? 0 : n <= 7 ? 1 : 2);
}

bool intents_is_name_word(const char *w)
{
    /* Como lo ha escrito la transcripción en tus logs. */
    static const char *const SEEN[] = {"akari", "okari", "sokar", "socar", "sakari", "sokary", "zachary", "sotori",
                                       "zockery", "zuckari", "zucari", "sukari", "sacaria", "zocari", "sokri"};
    /* Palabras de verdad que suenan parecido: nunca son el nombre. */
    static const char *const NOT[] = {"socorro", "sacara", "sacar", "secar", "sucio", "sakura", "zacarias", "cari",
                                      "karina", "carina", "sabado", "sacarlo", "sacarla", "socarron"};
    for (size_t i = 0; i < sizeof NOT / sizeof *NOT; i++)
        if (!strcmp(w, NOT[i])) return false;
    for (size_t i = 0; i < sizeof SEEN / sizeof *SEEN; i++)
        if (!strcmp(w, SEEN[i])) return true;
    char key[32];
    sound_key(w, key, sizeof key);
    size_t n = strlen(key);
    if (n < 5 || n > 8) return false;
    return edit_distance(key, "sokari") <= (n <= 5 ? 1 : 2);
}

static bool is_letter_start(const unsigned char *p)
{
    return isalnum(*p) || (*p == 0xC3 && p[1]);
}

char *intents_fix_name(const char *text)
{
    StrBuf sb;
    sb_init(&sb);
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    while (*p) {
        if (!is_letter_start(p)) {
            sb_append_n(&sb, (const char *)p, 1);
            p++;
            continue;
        }
        const unsigned char *s = p;
        while (*p && is_letter_start(p)) p += *p == 0xC3 ? 2 : 1;
        char word[64];
        size_t len = (size_t)(p - s);
        if (len >= sizeof word) {
            sb_append_n(&sb, (const char *)s, len);
            continue;
        }
        memcpy(word, s, len);
        word[len] = 0;
        char *norm = intents_normalize(word);
        char *t = str_trim(norm);
        sb_append(&sb, intents_is_name_word(t) ? "Sokari" : word);
        free(t);
        free(norm);
    }
    return sb.data ? sb.data : xstrdup("");
}

/* ------------------------------------------------------- vocabulario --- */

/* Lo que puede acompañar a cualquier comando sin cambiarlo. */
static const char *const FILLER =
    " y e o a al el la lo los las le les me mi mis tu te de del en con por favor porfa porfis plis please oye oiga "
    "hey ey eh ah oh uh hola que onda como andas estas esta tal va bien bueno pues ya ahora ahorita puedes podrias "
    "puedas pudieras quiero quisiera necesito haz hazme dale andale orale vamos ok okey sale si ahi aqui alla eso "
    "esto ese esa este un una poco poquito tantito nuevo nueva nuevos nuevas otra vez amigo amiga carnal compa bro "
    "wey guey chido "
    "rapido rapidito tambien porfavor sokari es se ves ve mira no muy cierto entonces asi porfis yo creo voy "
    "pobre sea refiero digo "
    /* Relleno mexicano: saludos, apodos y muletillas. */
    "pedo quiubo quihubo rollo hubo we carnal carnala mano manito chavo chava neta chale orale andale sale "
    "simon arre camara sobres chida padre chingon chingona manches mames pos pus fijate tons horita orita rato "
    "hijole ay aguas jefe jefa compadre rey reina hermano brother morro morra cuate cuata vato bato valedor ";

typedef struct {
    const char *pattern; /* palabras completas, con espacios alrededor */
    IntentKind kind;
    const char *arg;
} Trigger;

static const Trigger TRIGGERS[] = {
    {" quitale la pausa ", IN_PLAY, NULL},       {" quita la pausa ", IN_PLAY, NULL},
    {" play ", IN_PLAY, NULL},                   {" reanuda ", IN_PLAY, NULL},
    {" reanudalo ", IN_PLAY, NULL},              {" continua el video ", IN_PLAY, NULL},
    {" continua la musica ", IN_PLAY, NULL},     {" continua la cancion ", IN_PLAY, NULL},
    {" pausa ", IN_PAUSE, NULL},                 {" pausale ", IN_PAUSE, NULL},
    {" pausalo ", IN_PAUSE, NULL},               {" pausar ", IN_PAUSE, NULL},
    {" para el video ", IN_PAUSE, NULL},         {" para la musica ", IN_PAUSE, NULL},
    {" para la cancion ", IN_PAUSE, NULL},       {" parale ", IN_PAUSE, NULL},
    {" deten el video ", IN_PAUSE, NULL},        {" deten la musica ", IN_PAUSE, NULL},
    {" siguiente ", IN_NEXT, NULL},              {" la que sigue ", IN_NEXT, NULL},
    {" otra rola ", IN_NEXT, NULL},              {" ponme otra ", IN_NEXT, NULL},
    {" pon otra ", IN_NEXT, NULL},               {" cambiala ", IN_NEXT, NULL},
    {" regresale ", IN_PREV, NULL},              {" regresa la rola ", IN_PREV, NULL},
    {" mas recio ", IN_VOL_UP, NULL},            {" mas quedito ", IN_VOL_DOWN, NULL},
    {" quitale el volumen ", IN_MUTE, NULL},     {" grax ", IN_THANKS, NULL},
    {" se agradece ", IN_THANKS, NULL},          {" te la rifaste ", IN_THANKS, NULL},
    {" eres un crack ", IN_THANKS, NULL},        {" stop ", IN_PAUSE, NULL},
    {" mas duro ", IN_VOL_UP, NULL},             {" mas suave ", IN_VOL_DOWN, NULL},
    {" escondela ", IN_MINIMIZE, NULL},          {" escondelo ", IN_MINIMIZE, NULL},
    {" otra cancion ", IN_NEXT, NULL},           {" cambia la cancion ", IN_NEXT, NULL},
    {" cambiale ", IN_NEXT, NULL},               {" salta la cancion ", IN_NEXT, NULL},
    {" saltala ", IN_NEXT, NULL},                {" saltate ", IN_NEXT, NULL},
    {" pasa la cancion ", IN_NEXT, NULL},        {" skip ", IN_NEXT, NULL},
    {" anterior ", IN_PREV, NULL},               {" la de antes ", IN_PREV, NULL},
    {" regresa la cancion ", IN_PREV, NULL},     {" la pasada ", IN_PREV, NULL},
    {" sube el volumen ", IN_VOL_UP, NULL},      {" sube volumen ", IN_VOL_UP, NULL},
    {" subele ", IN_VOL_UP, NULL},               {" mas volumen ", IN_VOL_UP, NULL},
    {" mas fuerte ", IN_VOL_UP, NULL},           {" mas alto ", IN_VOL_UP, NULL},
    {" baja el volumen ", IN_VOL_DOWN, NULL},    {" baja volumen ", IN_VOL_DOWN, NULL},
    {" bajale ", IN_VOL_DOWN, NULL},             {" menos volumen ", IN_VOL_DOWN, NULL},
    {" mas bajo ", IN_VOL_DOWN, NULL},           {" mas bajito ", IN_VOL_DOWN, NULL},
    {" mutea ", IN_MUTE, NULL},                  {" silencia ", IN_MUTE, NULL},
    {" quita el sonido ", IN_MUTE, NULL},        {" quitale el sonido ", IN_MUTE, NULL},
    {" minimiza todo ", IN_MINIMIZE_ALL, NULL},  {" minimiza todas ", IN_MINIMIZE_ALL, NULL},
    {" minimizalo todo ", IN_MINIMIZE_ALL, NULL}, {" muestra el escritorio ", IN_MINIMIZE_ALL, NULL},
    {" ve al escritorio ", IN_MINIMIZE_ALL, NULL}, {" minimiza ", IN_MINIMIZE, NULL},
    {" minimizala ", IN_MINIMIZE, NULL},         {" minimizalo ", IN_MINIMIZE, NULL},
    {" minimizar ", IN_MINIMIZE, NULL},          {" maximiza ", IN_MAXIMIZE, NULL},
    {" maximizala ", IN_MAXIMIZE, NULL},         {" maximizalo ", IN_MAXIMIZE, NULL},
    {" maximizar ", IN_MAXIMIZE, NULL},          {" agrandala ", IN_MAXIMIZE, NULL},
    {" agranda la ventana ", IN_MAXIMIZE, NULL}, {" pantalla completa ", IN_FULLSCREEN, NULL},
    {" cierra la pestana ", IN_CLOSE_TAB, NULL}, {" cierra esta pestana ", IN_CLOSE_TAB, NULL},
    {" cierra pestana ", IN_CLOSE_TAB, NULL},    {" cierra la ventana ", IN_CLOSE_WINDOW, NULL},
    {" cierra esta ventana ", IN_CLOSE_WINDOW, NULL}, {" cierra el programa ", IN_CLOSE_WINDOW, NULL},
    {" cierra la app ", IN_CLOSE_WINDOW, NULL},  {" gracias ", IN_THANKS, NULL},
};

/* Las palabras que puede traer cada comando además del relleno. */
static const char *vocab(IntentKind k)
{
    switch (k) {
    case IN_PLAY:
    case IN_PAUSE:
        return " play pausa pausale pausalo pausar pausas reanuda reanudalo reanudar continua continuar quitale quita "
               "ponle pon ponla ponlo ponme ponerle poner darle video videos musica cancion canciones rola audio "
               "youtube spotify reproduccion deten para parale picale puchale pushale picalo puchalo stop ";
    case IN_NEXT:
    case IN_PREV:
        return " siguiente sigue otra cancion rola tema pista video cambia cambiale salta saltate saltala adelanta "
               "pasa pasale skip anterior antes regresa regresale pasada pon ponme ponle picale puchale pushale "
               "cambiala rolas ";
    case IN_VOL_UP:
    case IN_VOL_DOWN:
    case IN_VOL_SET:
    case IN_MUTE:
        return " sube subele subelo baja bajale bajalo volumen mas menos fuerte bajo bajito alto sonido audio musica "
               "mutea silencia quita quitale sin nivel ciento pon ponle ponlo computadora compu pc buen harto recio "
               "quedito maximo minimo tope picale puchale duro suave rayitas rayita dos tres ";
    case IN_MINIMIZE:
    case IN_MINIMIZE_ALL:
    case IN_MAXIMIZE:
    case IN_FULLSCREEN:
    case IN_CLOSE_TAB:
    case IN_CLOSE_WINDOW:
        return " minimiza minimizala minimizalo minimizar maximiza maximizala maximizalo maximizar agranda agrandala "
               "cierra cerrar ventana ventanas pestana pestanas pantalla completa app aplicacion programa navegador "
               "todo todas muestra ve escritorio pon ponlo ponla video escondela escondelo ";
    case IN_EXPLORER:
    case IN_FOLDER:
        return " abre abreme abrir explorador archivos equipo pc computadora descargas documentos escritorio imagenes "
               "fotos carpeta carpetas ";
    case IN_OPEN_APP:
        return " abre abreme abrir abrela abrelo pestana ventana app aplicacion programa navegador abierta abierto "
               "abiertas ";
    case IN_THANKS:
        return " gracias muchas mil muy perfecto excelente buenisimo genial grax agradece rifaste te la se eres un "
               "crack ";
    case IN_KEYS: return " "; /* se entiende aparte: ver parse_keys */
    }
    return " ";
}

static bool has(const char *norm, const char *pattern)
{
    return strstr(norm, pattern) != NULL;
}

static bool is_number(const char *w)
{
    size_t n = strlen(w);
    if (!n || n > 3) return false;
    for (const char *p = w; *p; p++)
        if (!isdigit((unsigned char)*p)) return false;
    return atoi(w) <= 100;
}

static void add_item(IntentList *l, IntentKind k, const char *arg, int pos)
{
    for (int i = 0; i < l->n; i++)
        if (l->items[i].kind == k) return;
    if (l->n >= (int)(sizeof l->items / sizeof *l->items)) return;
    l->items[l->n].kind = k;
    snprintf(l->items[l->n].arg, sizeof l->items[l->n].arg, "%s", arg ? arg : "");
    l->items[l->n].pos = pos;
    l->n++;
}

static bool in_list(const char *list, const char *w)
{
    char pat[64];
    snprintf(pat, sizeof pat, " %s ", w);
    return strstr(list, pat) != NULL;
}

/* ------------------------------------------------------------ parser --- */

/* "no le pongas pausa", "nunca minimices": un no justo antes de un comando.
   ("No lo estás haciendo, ponle play" no cuenta: el no va con otra cosa.) */
static bool negated(const char *norm)
{
    static const char *const ROOTS[] = {"pon", "play", "paus", "minimi", "maximi", "cierr", "cerr", "abr", "sub",
                                        "baj", "quit", "reanud", "cambi", "salt", "silenci", "mute", "des", "agrand"};
    for (const char *p = norm; *p; p++) {
        if (*p != ' ' || !p[1]) continue;
        char w0[16], w1[24] = "", w2[24] = "";
        if (sscanf(p + 1, "%15s %23s %23s", w0, w1, w2) < 2) continue;
        if (strcmp(w0, "no") && strcmp(w0, "nunca") && strcmp(w0, "tampoco")) continue;
        /* "No mames", "no manches", "no inventes": expresiones, no un no. */
        if (in_list(" mames manches inventes friegues chingues jodas mamen ", w1)) continue;
        for (size_t i = 0; i < sizeof ROOTS / sizeof *ROOTS; i++)
            if (str_starts_with(w1, ROOTS[i]) || str_starts_with(w2, ROOTS[i])) return true;
    }
    return false;
}

static bool in_any_vocab(const char *w)
{
    for (int k = IN_PLAY; k <= IN_THANKS; k++)
        if (in_list(vocab((IntentKind)k), w)) return true;
    return false;
}

/* ------------------------------------------------------------- teclas --- */

/* "Oprime windows", "dale enter", "presiona escape tres veces", "control
   zeta": una tecla o un atajo, dicho solo. Sin verbo tiene que ser una
   combinación (un "abajo" suelto puede ser otra cosa). Lo que borra (Supr,
   Ctrl+D) no se hace aquí: eso siempre se confirma. */
static const char *const KEY_VERBS =
    " oprime oprimele presiona presionale aprieta aprietale pulsa pulsale dale picale puchale pushale teclea ";
static const char *const KEY_TAIL = " por favor porfa porfis plis please ya ahora ahorita gracias ";

static int times_word(const char *w)
{
    static const char *const N[] = {"una", "dos", "tres", "cuatro", "cinco", "seis", "siete", "ocho", "nueve", "diez"};
    for (int i = 0; i < 10; i++)
        if (!strcmp(w, N[i])) return i + 1;
    if (!strcmp(w, "un") || !strcmp(w, "uno") || !strcmp(w, "otra")) return 1;
    return is_number(w) ? atoi(w) : 0;
}

static bool parse_keys(const char *norm, IntentList *out)
{
    char *copy = xstrdup(norm), *w[16];
    int n = 0;
    bool ok = false;
    for (char *t = strtok(copy, " "); t; t = strtok(NULL, " ")) {
        if (n == 16) goto done;
        w[n++] = t;
    }
    int i = 0;
    bool verb = false;
    for (; i < n; i++) {
        if (!strcmp(w[i], "no") || !strcmp(w[i], "nunca") || !strcmp(w[i], "tampoco")) goto done;
        if (in_list(KEY_VERBS, w[i])) {
            verb = true;
            i++;
            break;
        }
        if (!in_list(FILLER, w[i]) && !intents_is_name_word(w[i])) break;
    }
    int end = n, times = 1;
    bool counted = false;
    for (;;) {
        while (end > i && (in_list(KEY_TAIL, w[end - 1]) || intents_is_name_word(w[end - 1]))) end--;
        if (!counted && end - i >= 3 && (!strcmp(w[end - 1], "veces") || !strcmp(w[end - 1], "vez"))) {
            times = times_word(w[end - 2]);
            if (times < 1 || times > 20) goto done;
            end -= 2;
            counted = true;
            continue;
        }
        break;
    }
    if (end <= i) goto done;
    char spec[sizeof out->items[0].arg];
    spec[0] = 0;
    for (int k = i; k < end; k++) {
        size_t len = strlen(spec);
        if (len + strlen(w[k]) + 2 > sizeof spec) goto done;
        snprintf(spec + len, sizeof spec - len, "%s%s", len ? " " : "", w[k]);
    }
    KeyCombo kc;
    if (!keys_parse(spec, &kc) || kc.has_delete || keys_does_nothing(&kc)) goto done;
    if (!verb && !keys_is_combo(&kc)) goto done;
    add_item(out, IN_KEYS, spec, 0);
    out->items[0].times = times;
    ok = true;
done:
    free(copy);
    return ok;
}

static const char *const OPEN_WORDS[] = {" abre ", " abreme ", " abrir ", " abrirme "};
static const char *const FOLDER_WORDS[] = {"descargas", "documentos", "escritorio", "imagenes", "fotos"};

bool intents_parse(const char *text, IntentList *out)
{
    memset(out, 0, sizeof *out);
    char *norm = intents_normalize(text);
    bool ok = false;
    /* Una pregunta de verdad ("¿qué es play?") o un comando negado ("no le
       pongas pausa") los entiende mejor el modelo. */
    static const char *const SKIP[] = {" que es ", " por que ", " para que ", " como se ", " cuanto ", " cual ", " cuales "};
    for (size_t i = 0; i < sizeof SKIP / sizeof *SKIP; i++)
        if (has(norm, SKIP[i])) goto done;
    if (negated(norm)) goto done;

    /* Cada palabra: cuántas hay (más de 20 ya no es un comando simple). */
    int words = 0;
    for (const char *p = norm; *p; p++)
        if (*p != ' ' && p[-1] == ' ') words++;
    if (!words || words > 20) goto done;

    out->greeting = has(norm, " como andas ") || has(norm, " como estas ") || has(norm, " que onda ") ||
                    has(norm, " que tal ") || has(norm, " como te va ") || has(norm, " como va ");
    if (parse_keys(norm, out)) {
        ok = true;
        goto done;
    }

    for (size_t i = 0; i < sizeof TRIGGERS / sizeof *TRIGGERS; i++) {
        if (!has(norm, TRIGGERS[i].pattern)) continue;
        IntentKind k = TRIGGERS[i].kind;
        if (k == IN_MINIMIZE && has(norm, " todo ")) k = IN_MINIMIZE_ALL;
        add_item(out, k, TRIGGERS[i].arg, (int)(strstr(norm, TRIGGERS[i].pattern) - norm));
    }
    /* play y pausa son la misma tecla: queda solo lo que dijiste primero. */
    bool play = false, pause = false;
    for (int i = 0; i < out->n; i++) {
        play |= out->items[i].kind == IN_PLAY;
        pause |= out->items[i].kind == IN_PAUSE;
    }
    if (play && pause) {
        const char *pp = strstr(norm, " play "), *pz = strstr(norm, " paus");
        IntentKind drop = pp && pz && pp < pz ? IN_PAUSE : IN_PLAY;
        for (int i = 0; i < out->n; i++)
            if (out->items[i].kind == drop) out->items[i--] = out->items[--out->n];
    }
    if ((has(norm, " minimiza") || has(norm, " minimizar ")) && out->n) {
        for (int i = 0; i < out->n; i++)
            if (out->items[i].kind == IN_FULLSCREEN) out->items[i--] = out->items[--out->n];
    }
    /* "Súbele al máximo" / "bájale al mínimo": un nivel exacto. */
    for (int i = 0; i < out->n; i++) {
        bool up = out->items[i].kind == IN_VOL_UP, down = out->items[i].kind == IN_VOL_DOWN;
        if (up && (has(norm, " maximo ") || has(norm, " al tope ") || has(norm, " a todo ")))
            out->items[i].kind = IN_VOL_SET, snprintf(out->items[i].arg, sizeof out->items[i].arg, "100");
        else if (down && has(norm, " minimo "))
            out->items[i].kind = IN_VOL_SET, snprintf(out->items[i].arg, sizeof out->items[i].arg, "5");
    }
    const char *vol = strstr(norm, " volumen ");
    if (vol) {
        char w1[8] = "", w2[8] = "", w3[8] = "";
        sscanf(vol + 9, "%7s %7s %7s", w1, w2, w3);
        const char *num = is_number(w1) ? w1 : is_number(w2) ? w2 : is_number(w3) ? w3 : NULL;
        if (num) {
            for (int i = 0; i < out->n; i++)
                if (out->items[i].kind == IN_VOL_UP || out->items[i].kind == IN_VOL_DOWN)
                    out->items[i--] = out->items[--out->n];
            add_item(out, IN_VOL_SET, num, (int)(vol - norm));
        }
    }

    /* Abrir: el Explorador, una carpeta tuya o una app por su nombre. */
    const char *open = NULL;
    for (size_t i = 0; i < sizeof OPEN_WORDS / sizeof *OPEN_WORDS && !open; i++) open = strstr(norm, OPEN_WORDS[i]);
    char app[48] = "";
    if (open) {
        const char *folder = NULL;
        for (size_t i = 0; i < sizeof FOLDER_WORDS / sizeof *FOLDER_WORDS && !folder; i++)
            if (in_list(norm, FOLDER_WORDS[i])) folder = FOLDER_WORDS[i];
        if (has(norm, " explorador ") || has(norm, " archivos ") || has(norm, " este equipo ") ||
            has(norm, " mi pc ") || has(norm, " mi computadora ")) {
            add_item(out, IN_EXPLORER, NULL, (int)(open - norm));
        } else if (folder) {
            add_item(out, IN_FOLDER, folder, (int)(open - norm));
        } else {
            /* "abre (la pestaña de) opera gx": hasta dos palabras que no son relleno. */
            const char *p = strchr(open + 1, ' ');
            int taken = 0;
            while (p && p[1] && taken < 2) {
                char w[32];
                if (sscanf(p + 1, "%31s", w) != 1) break;
                /* "spotify" o "youtube" también son palabras de música, pero aquí son la app. */
                bool app_name = in_list(" spotify youtube ", w);
                bool skip = in_list(FILLER, w) || (in_any_vocab(w) && !app_name) || intents_is_name_word(w);
                if (skip && taken) break;
                if (!skip) {
                    size_t len = strlen(app);
                    snprintf(app + len, sizeof app - len, "%s%s", taken ? " " : "", w);
                    taken++;
                }
                p = strchr(p + 1, ' ');
            }
            if (!taken && has(norm, " navegador ")) snprintf(app, sizeof app, "navegador");
            if (*app) add_item(out, IN_OPEN_APP, app, (int)(open - norm));
        }
    }
    if (!out->n) goto done;
    for (int i = 1; i < out->n; i++) /* en el orden en que lo dijiste */
        for (int j = i; j > 0 && out->items[j - 1].pos > out->items[j].pos; j--) {
            IntentItem t = out->items[j];
            out->items[j] = out->items[j - 1];
            out->items[j - 1] = t;
        }
    /* "Gracias" solo cuenta si no pediste nada más. */
    if (out->n > 1)
        for (int i = 0; i < out->n; i++)
            if (out->items[i].kind == IN_THANKS) out->items[i--] = out->items[--out->n];

    /* Lo que sobra tiene que ser relleno o parte de estos comandos; se
       perdona una palabra de más (un "chinga" o un nombre raro). */
    char app_words[56];
    snprintf(app_words, sizeof app_words, " %s ", app);
    char seen[8][32];
    int unknown = 0;
    for (const char *p = norm + 1; *p;) {
        const char *e = strchr(p, ' ');
        char w[32];
        size_t len = (size_t)(e - p);
        if (len >= sizeof w) len = sizeof w - 1;
        memcpy(w, p, len);
        w[len] = 0;
        bool known = in_list(FILLER, w) || intents_is_name_word(w) || (*app && in_list(app_words, w)) ||
                     (vol && is_number(w));
        for (int i = 0; i < out->n && !known; i++) known = in_list(vocab(out->items[i].kind), w);
        for (int i = 0; i < unknown && !known; i++) known = !strcmp(seen[i], w);
        if (!known && unknown < 8) memcpy(seen[unknown++], w, sizeof seen[0]);
        p = e + 1;
    }
    ok = unknown <= (words >= 10 ? 2 : 1);
done:
    if (!ok) memset(out, 0, sizeof *out);
    free(norm);
    return ok;
}

/* ---------------------------------------------------------- ejecutar --- */

static char *tool(const char *name, const char *args)
{
    char *r = run_tool(name, args);
    return r ? r : xstrdup("");
}

char *intents_run(const IntentList *l, bool *handled)
{
    *handled = true;
    StrBuf sb;
    sb_init(&sb);
    if (l->greeting) sb_append(&sb, "¡Todo bien! ");
    for (int i = 0; i < l->n && *handled; i++) {
        const IntentItem *it = &l->items[i];
        char args[128];
        char *r = NULL;
        const char *say = NULL;
        switch (it->kind) {
        case IN_PLAY: r = tool("control_media", "{\"action\":\"play_pause\"}"), say = "Le di play."; break;
        case IN_PAUSE: r = tool("control_media", "{\"action\":\"play_pause\"}"), say = "Le puse pausa."; break;
        case IN_NEXT: r = tool("control_media", "{\"action\":\"next_track\"}"), say = "Va la siguiente."; break;
        case IN_PREV: r = tool("control_media", "{\"action\":\"previous_track\"}"), say = "Va la anterior."; break;
        case IN_VOL_UP:
        case IN_VOL_DOWN: {
            /* Cada tecla mueve 2 %: cinco son un cambio que sí se nota. */
            const char *a = it->kind == IN_VOL_UP ? "{\"action\":\"volume_up\"}" : "{\"action\":\"volume_down\"}";
            for (int k = 0; k < 5; k++) {
                free(r);
                r = tool("control_media", a);
            }
            say = it->kind == IN_VOL_UP ? "Subí el volumen." : "Bajé el volumen.";
            break;
        }
        case IN_VOL_SET:
            snprintf(args, sizeof args, "{\"action\":\"set_volume\",\"nivel\":%d}", atoi(it->arg));
            r = tool("control_media", args);
            break;
        case IN_MUTE: r = tool("control_media", "{\"action\":\"mute\"}"), say = "Listo."; break;
        case IN_MINIMIZE: r = tool("control_desktop", "{\"action\":\"minimize\"}"); break;
        case IN_MINIMIZE_ALL: r = tool("control_desktop", "{\"action\":\"minimize_all\"}"), say = "Minimicé todo."; break;
        case IN_MAXIMIZE: r = tool("control_desktop", "{\"action\":\"maximize\"}"); break;
        case IN_FULLSCREEN: r = tool("control_desktop", "{\"action\":\"fullscreen\"}"), say = "Listo."; break;
        case IN_CLOSE_TAB: r = tool("control_desktop", "{\"action\":\"close_tab\"}"), say = "Cerré la pestaña."; break;
        case IN_CLOSE_WINDOW: r = tool("control_desktop", "{\"action\":\"close_window\"}"); break;
        case IN_EXPLORER: r = tool("open_app", "{\"name\":\"explorador\"}"); break;
        case IN_FOLDER:
        case IN_OPEN_APP:
            snprintf(args, sizeof args, "{\"name\":\"%s\"}", it->arg); /* solo letras y números: ver intents_normalize */
            r = tool("open_app", args);
            /* Si no existe una app con ese nombre, que lo resuelva el modelo. */
            if (it->kind == IN_OPEN_APP && (str_starts_with(r, "No encontré") || str_starts_with(r, "Por seguridad")))
                *handled = false;
            break;
        case IN_THANKS: say = l->greeting ? "Gracias a ti." : "De nada."; break;
        case IN_KEYS:
            /* arg: solo letras, números y espacios (ver parse_keys) */
            snprintf(args, sizeof args, "{\"teclas\":\"%s\",\"veces\":%d}", it->arg, it->times > 1 ? it->times : 1);
            r = tool("presionar_teclas", args);
            break;
        }
        /* Si la herramienta no contestó "Listo.", se dice lo que contestó. */
        const char *text = say && (!r || !*r || str_starts_with(r, "Listo")) ? say : r;
        if (text && *text) sb_appendf(&sb, "%s%s", sb.len && sb.data[sb.len - 1] != ' ' ? " " : "", text);
        free(r);
    }
    if (!*handled) {
        sb_free(&sb);
        return NULL;
    }
    return sb.data;
}
