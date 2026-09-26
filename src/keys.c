#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "intents.h"
#include "keys.h"
#include "util.h"

enum { K_MOD = 1, K_DEL = 2, K_ENTER = 4 };

typedef struct {
    const char *name; /* normalizado: minúsculas, sin acentos, palabras separadas por un espacio */
    unsigned short vk;
    unsigned char flags;
} KeyName;

static const KeyName NAMES[] = {
    {"control", VK_CONTROL, K_MOD}, {"ctrl", VK_CONTROL, K_MOD}, {"alt", VK_MENU, K_MOD},
    {"shift", VK_SHIFT, K_MOD}, {"mayus", VK_SHIFT, K_MOD}, {"mayusculas", VK_SHIFT, K_MOD},
    {"windows", VK_LWIN, K_MOD}, {"win", VK_LWIN, K_MOD},
    {"enter", VK_RETURN, K_ENTER}, {"intro", VK_RETURN, K_ENTER}, {"entrar", VK_RETURN, K_ENTER},
    {"return", VK_RETURN, K_ENTER},
    {"escape", VK_ESCAPE, 0}, {"esc", VK_ESCAPE, 0}, {"escapar", VK_ESCAPE, 0},
    {"tab", VK_TAB, 0}, {"tabulador", VK_TAB, 0}, {"tabulacion", VK_TAB, 0},
    {"barra espaciadora", VK_SPACE, 0}, {"espacio", VK_SPACE, 0}, {"space", VK_SPACE, 0},
    {"retroceso", VK_BACK, 0}, {"backspace", VK_BACK, 0}, {"borrar", VK_BACK, 0},
    {"suprimir", VK_DELETE, K_DEL}, {"supr", VK_DELETE, K_DEL}, {"delete", VK_DELETE, K_DEL}, {"del", VK_DELETE, K_DEL},
    {"insertar", VK_INSERT, 0}, {"insert", VK_INSERT, 0},
    {"inicio", VK_HOME, 0}, {"home", VK_HOME, 0}, {"fin", VK_END, 0}, {"end", VK_END, 0},
    {"re pag", VK_PRIOR, 0}, {"repag", VK_PRIOR, 0}, {"pagina arriba", VK_PRIOR, 0}, {"page up", VK_PRIOR, 0},
    {"av pag", VK_NEXT, 0}, {"avpag", VK_NEXT, 0}, {"pagina abajo", VK_NEXT, 0}, {"page down", VK_NEXT, 0},
    {"flecha arriba", VK_UP, 0}, {"arriba", VK_UP, 0}, {"up", VK_UP, 0},
    {"flecha abajo", VK_DOWN, 0}, {"abajo", VK_DOWN, 0}, {"down", VK_DOWN, 0},
    {"flecha izquierda", VK_LEFT, 0}, {"izquierda", VK_LEFT, 0}, {"left", VK_LEFT, 0},
    {"flecha derecha", VK_RIGHT, 0}, {"derecha", VK_RIGHT, 0}, {"right", VK_RIGHT, 0},
    {"imprimir pantalla", VK_SNAPSHOT, 0}, {"impr pant", VK_SNAPSHOT, 0}, {"print screen", VK_SNAPSHOT, 0},
    {"bloq mayus", VK_CAPITAL, 0}, {"caps lock", VK_CAPITAL, 0},
    {"mas", VK_OEM_PLUS, 0}, {"plus", VK_OEM_PLUS, 0}, {"menos", VK_OEM_MINUS, 0}, {"minus", VK_OEM_MINUS, 0},
    {"punto", VK_OEM_PERIOD, 0}, {"coma", VK_OEM_COMMA, 0}, {"menu", VK_APPS, 0},
    /* Las letras como se dicen en español. */
    {"be", 'B', 0}, {"ce", 'C', 0}, {"efe", 'F', 0}, {"ge", 'G', 0}, {"hache", 'H', 0}, {"jota", 'J', 0},
    {"ka", 'K', 0}, {"ele", 'L', 0}, {"eme", 'M', 0}, {"ene", 'N', 0}, {"pe", 'P', 0}, {"cu", 'Q', 0},
    {"erre", 'R', 0}, {"ere", 'R', 0}, {"ese", 'S', 0}, {"te", 'T', 0}, {"uve", 'V', 0}, {"ve", 'V', 0},
    {"doble ve", 'W', 0}, {"doble u", 'W', 0}, {"uve doble", 'W', 0}, {"equis", 'X', 0}, {"ye", 'Y', 0},
    {"i griega", 'Y', 0}, {"zeta", 'Z', 0}, {"de", 'D', 0},
    {"cero", '0', 0}, {"uno", '1', 0}, {"dos", '2', 0}, {"tres", '3', 0}, {"cuatro", '4', 0},
    {"cinco", '5', 0}, {"seis", '6', 0}, {"siete", '7', 0}, {"ocho", '8', 0}, {"nueve", '9', 0},
};

/* Lo que se dice alrededor de las teclas y no es tecla. */
static const char *const SKIP = " la el al las los tecla teclas boton botones the key keys ";
/* Palabras cortas que son letra ("control y") o solo unen ("control y zeta"):
   son letra si son la última. */
static const char *const AMBIGUOUS = " a e o y de i u ";

static bool word_in(const char *list, const char *w)
{
    char pat[48];
    snprintf(pat, sizeof pat, " %s ", w);
    return strstr(list, pat) != NULL;
}

static int number_word(const char *w)
{
    static const char *const N[] = {"cero", "uno", "dos",  "tres", "cuatro", "cinco", "seis",
                                    "siete", "ocho", "nueve", "diez", "once", "doce"};
    for (int i = 0; i < 13; i++)
        if (!strcmp(w, N[i])) return i;
    if (isdigit((unsigned char)w[0]) && (!w[1] || (isdigit((unsigned char)w[1]) && !w[2]))) return atoi(w);
    return -1;
}

static bool add_key(KeyCombo *k, unsigned short vk, unsigned char flags)
{
    for (int i = 0; i < k->n; i++)
        if (k->vk[i] == vk) return true;
    if (k->n >= KEYS_MAX) return false;
    k->vk[k->n++] = vk;
    if (flags & K_DEL) k->has_delete = true;
    if (flags & K_ENTER) k->has_enter = true;
    return true;
}

static bool is_modifier(unsigned short vk)
{
    return vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT || vk == VK_LWIN;
}

/* Los signos que son tecla: "ctrl++" o "Control +" (zoom), "ctrl+-",
   "windows+." (emojis). Un + o un - antes de otra tecla solo las separa
   ("ctrl+shift+t", "ctrl-c"), y un punto o una coma son tecla solo justo
   después de un + ("Oprime Windows." termina en punto y no es la tecla). */
static char *spell_symbols(const char *s)
{
    StrBuf sb;
    sb_init(&sb);
    char prev = 0; /* el último carácter que no es espacio */
    for (const char *p = s; *p; p++) {
        char c = *p;
        const char *q = p + 1;
        while (*q == ' ') q++;
        bool key_after = *q && (isalnum((unsigned char)*q) || (unsigned char)*q >= 0x80 || strchr("+-.,", *q));
        if ((c == '+' || c == '-') && key_after) sb_append_char(&sb, ' ');
        else if (c == '+') sb_append(&sb, " mas ");
        else if (c == '-') sb_append(&sb, " menos ");
        else if ((c == '.' || c == ',') && prev == '+') sb_append(&sb, c == '.' ? " punto " : " coma ");
        else sb_append_char(&sb, c);
        if (c != ' ') prev = c;
    }
    return sb.data ? sb.data : xstrdup("");
}

bool keys_parse(const char *spec, KeyCombo *out)
{
    memset(out, 0, sizeof *out);
    char *spelled = spell_symbols(spec ? spec : "");
    char *norm = intents_normalize(spelled); /* " control k " */
    free(spelled);
    char *words[24];
    int n = 0;
    for (char *tok = strtok(norm, " "); tok && n < 24; tok = strtok(NULL, " ")) words[n++] = tok;
    bool ok = n > 0;
    for (int i = 0; i < n && ok;) {
        if (word_in(SKIP, words[i])) {
            i++;
            continue;
        }
        /* F1–F12: "f5", "f 5" o "efe cinco". */
        int fnum = -1, used = 0;
        if (words[i][0] == 'f' && words[i][1] && number_word(words[i] + 1) > 0) fnum = number_word(words[i] + 1), used = 1;
        else if ((!strcmp(words[i], "f") || !strcmp(words[i], "efe")) && i + 1 < n && number_word(words[i + 1]) > 0)
            fnum = number_word(words[i + 1]), used = 2;
        if (fnum >= 1 && fnum <= 12) {
            ok = add_key(out, (unsigned short)(VK_F1 + fnum - 1), 0);
            i += used;
            continue;
        }
        if (word_in(AMBIGUOUS, words[i]) && i + 1 < n) {
            i++;
            continue;
        }
        /* El nombre más largo que empiece aquí ("flecha abajo" antes que "abajo"). */
        int best = -1, best_len = 0;
        for (size_t k = 0; k < sizeof NAMES / sizeof *NAMES; k++) {
            char buf[64] = "";
            for (int len = 1; len <= 3 && i + len <= n; len++) {
                size_t bl = strlen(buf);
                snprintf(buf + bl, sizeof buf - bl, "%s%s", len > 1 ? " " : "", words[i + len - 1]);
                if (!strcmp(buf, NAMES[k].name) && len > best_len) best = (int)k, best_len = len;
            }
        }
        if (best >= 0) {
            ok = add_key(out, NAMES[best].vk, NAMES[best].flags);
            i += best_len;
        } else if (!words[i][1] && isalnum((unsigned char)words[i][0])) {
            ok = add_key(out, (unsigned short)toupper((unsigned char)words[i][0]), 0);
            i++;
        } else {
            ok = false; /* una palabra que no es tecla: mejor no oprimir nada */
        }
    }
    free(norm);
    /* Primero los modificadores; y a lo más una tecla que no lo sea. */
    int others = 0;
    unsigned short ordered[KEYS_MAX];
    int m = 0;
    for (int i = 0; i < out->n; i++)
        if (is_modifier(out->vk[i])) ordered[m++] = out->vk[i];
    for (int i = 0; i < out->n; i++)
        if (!is_modifier(out->vk[i])) ordered[m++] = out->vk[i], others++;
    memcpy(out->vk, ordered, sizeof ordered[0] * (size_t)m);
    if (!ok || !out->n || others > 1) {
        memset(out, 0, sizeof *out);
        return false;
    }
    /* En el Explorador, Ctrl+D también manda a la papelera. */
    if (out->n == 2 && out->vk[0] == VK_CONTROL && out->vk[1] == 'D') out->has_delete = true;
    return true;
}

bool keys_is_combo(const KeyCombo *k)
{
    return k->n >= 2 && is_modifier(k->vk[0]) && !is_modifier(k->vk[k->n - 1]);
}

bool keys_does_nothing(const KeyCombo *k)
{
    for (int i = 0; i < k->n; i++)
        if (k->vk[i] != VK_CONTROL && k->vk[i] != VK_SHIFT) return false;
    return true;
}

char *keys_describe(const KeyCombo *k)
{
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < k->n; i++) {
        unsigned short vk = k->vk[i];
        char one[24];
        const char *name = NULL;
        switch (vk) {
        case VK_CONTROL: name = "Ctrl"; break;
        case VK_MENU: name = "Alt"; break;
        case VK_SHIFT: name = "Shift"; break;
        case VK_LWIN: name = "Windows"; break;
        case VK_RETURN: name = "Enter"; break;
        case VK_ESCAPE: name = "Esc"; break;
        case VK_TAB: name = "Tab"; break;
        case VK_SPACE: name = "Espacio"; break;
        case VK_BACK: name = "Retroceso"; break;
        case VK_DELETE: name = "Supr"; break;
        case VK_INSERT: name = "Insert"; break;
        case VK_HOME: name = "Inicio"; break;
        case VK_END: name = "Fin"; break;
        case VK_PRIOR: name = "RePág"; break;
        case VK_NEXT: name = "AvPág"; break;
        case VK_UP: name = "Flecha arriba"; break;
        case VK_DOWN: name = "Flecha abajo"; break;
        case VK_LEFT: name = "Flecha izquierda"; break;
        case VK_RIGHT: name = "Flecha derecha"; break;
        case VK_SNAPSHOT: name = "Impr Pant"; break;
        case VK_CAPITAL: name = "Bloq Mayús"; break;
        case VK_OEM_PLUS: name = "+"; break;
        case VK_OEM_MINUS: name = "-"; break;
        case VK_OEM_PERIOD: name = "."; break;
        case VK_OEM_COMMA: name = ","; break;
        case VK_APPS: name = "Menú"; break;
        default:
            if (vk >= VK_F1 && vk <= VK_F12) snprintf(one, sizeof one, "F%d", vk - VK_F1 + 1);
            else snprintf(one, sizeof one, "%c", (char)vk);
            name = one;
        }
        sb_appendf(&sb, "%s%s", i ? "+" : "", name);
    }
    return sb.data ? sb.data : xstrdup("");
}

int keys_find_tab(const char *want, TabTitleFn title, TabNextFn next, void *ctx, int max)
{
    char *w = intents_normalize(want), *wt = str_trim(w);
    free(w);
    char *first = NULL;
    int found = -1;
    for (int i = 0; i <= max && *wt; i++) {
        char *t = title(ctx);
        if (!t) break; /* ya no se puede seguir (por ejemplo, cambiaste de ventana) */
        char *tn = intents_normalize(t);
        free(t);
        bool hit = strstr(tn, wt) != NULL;
        /* Si ya dio la vuelta (volvió a la primera), no está. */
        bool around = first && i > 0 && !strcmp(first, tn);
        if (!first) first = xstrdup(tn);
        free(tn);
        if (hit) {
            found = i;
            break;
        }
        if (around || i == max) break;
        next(ctx);
    }
    free(first);
    free(wt);
    return found;
}

/* ------------------------------------------------------------- atajos --- */

typedef struct {
    const char *apps; /* palabras que la nombran */
    const char *text;
} Shortcuts;

static const Shortcuts SHORTCUTS[] = {
    {" navegador chrome edge opera firefox brave vivaldi pestana pestanas ",
     "Navegador: Ctrl+T pestaña nueva; Ctrl+W cierra la pestaña; Ctrl+Shift+T reabre la que cerraste; Ctrl+Tab la "
     "siguiente y Ctrl+Shift+Tab la anterior; Ctrl+1 a Ctrl+8 va a esa pestaña y Ctrl+9 a la última; Ctrl+L la barra "
     "de direcciones; Ctrl+F buscar en la página; F5 recargar; Alt+Flecha izquierda atrás; Ctrl+Shift+N (Chrome, "
     "Edge, Opera) o Ctrl+Shift+P (Firefox) ventana privada; Ctrl+más y Ctrl+menos zoom."},
    {" youtube video videos ",
     "YouTube (con la página enfrente): K play/pausa; J y L 10 s atrás/adelante; flechas 5 s; F pantalla completa; M "
     "silencio; Shift+N siguiente video; / va a la búsqueda."},
#ifdef _WIN32
    {" explorador archivos carpeta carpetas descargas documentos ",
     "Explorador: Ctrl+L barra de direcciones; Ctrl+E buscar; Ctrl+N ventana nueva; Ctrl+Shift+N carpeta nueva; F2 "
     "renombrar; Alt+Flecha arriba subir una carpeta; Alt+Enter propiedades; Ctrl+C / Ctrl+V copiar y pegar."},
#else
    {" explorador archivos nautilus carpeta carpetas descargas documentos ",
     "Archivos (Nautilus): Ctrl+L barra de direcciones; Ctrl+F buscar; Ctrl+N ventana nueva; Ctrl+T pestaña nueva; "
     "Ctrl+Shift+N carpeta nueva; F2 renombrar; Alt+Flecha arriba subir una carpeta; Ctrl+H mostrar los ocultos; "
     "Supr manda a la papelera; Ctrl+C / Ctrl+V copiar y pegar."},
#endif
    {" discord ",
     "Discord: Ctrl+K busca un chat o canal (escribe el nombre y Enter); Alt+Flecha arriba/abajo canal anterior o "
     "siguiente; Ctrl+Shift+M silencia tu micrófono; Ctrl+Shift+D ensordece; Esc marca como leído. Para mandar un "
     "archivo: cópialo (subir_archivo lo hace) y Ctrl+V en el chat."},
    {" spotify musica ",
     "Spotify (la app de escritorio): Espacio play/pausa; Ctrl+Flecha derecha/izquierda siguiente/anterior; "
     "Ctrl+Flecha arriba/abajo volumen; Ctrl+L o Ctrl+K buscar (según la versión)."},
    {" word office excel powerpoint documento ",
     "Word y Office: Ctrl+Z deshacer; Ctrl+C/Ctrl+V copiar y pegar; Ctrl+P imprimir. Guardar y formato cambian con el "
     "idioma: en español Ctrl+G guarda, Ctrl+N negrita, Ctrl+K cursiva, Ctrl+S subrayado; en inglés Ctrl+S guarda, "
     "Ctrl+B negrita, Ctrl+I cursiva, Ctrl+U subrayado."},
#ifdef _WIN32
    {" windows escritorio ventana ventanas pc compu computadora sistema ",
     "Windows: Windows abre Inicio; Windows+D escritorio; Windows+E Explorador; Windows+L bloquear; Alt+Tab cambiar "
     "de ventana; Alt+F4 cerrar; Windows+Shift+S recorte de pantalla; Windows+V historial del portapapeles; Windows+. "
     "emojis; Ctrl+Shift+Esc administrador de tareas; Windows+flechas acomodar la ventana; Windows+Tab vista de "
     "tareas."},
#else
    {" gnome linux ubuntu fedora windows escritorio ventana ventanas pc compu computadora sistema ",
     "GNOME (Ubuntu y Fedora): Windows (la tecla Super) abre Actividades y la búsqueda; Windows+A todas las apps; "
     "Alt+Tab cambia de app; Alt+F4 cierra; Windows+L bloquear; Windows+flecha arriba maximiza y Windows+flechas "
     "izquierda/derecha la acomodan a media pantalla; Windows+H minimiza; Windows+D muestra el escritorio (en "
     "Ubuntu); Impr Pant captura de pantalla; Windows+V notificaciones; Ctrl+Alt+T terminal; Alt+F2 «Ejecutar un "
     "comando»."},
#endif
};

const char *keys_shortcuts_for(const char *app)
{
    char *norm = intents_normalize(app);
    const char *found = NULL;
    for (size_t i = 0; i < sizeof SHORTCUTS / sizeof *SHORTCUTS && !found; i++) {
        char *p = norm;
        while (*p && !found) {
            while (*p == ' ') p++;
            char *e = strchr(p, ' ');
            if (!e) break;
            char w[40];
            size_t len = (size_t)(e - p);
            if (len && len < sizeof w) {
                memcpy(w, p, len);
                w[len] = 0;
                if (word_in(SHORTCUTS[i].apps, w)) found = SHORTCUTS[i].text;
            }
            p = e;
        }
    }
    free(norm);
    return found;
}
