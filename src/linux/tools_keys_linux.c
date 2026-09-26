/* Teclas y atajos para cualquier app, cambiar de pestaña y subir un archivo a
   un chat en Linux, con la extensión de GNOME. Las mismas reglas que en
   Windows: Sokari no ve la pantalla, así que dice en qué app oprimió (nunca
   el título de la ventana, que lo pone cada página) y no afirma lo que no
   puede saber; no le da Enter a terminales; y si en la conversación hay algo
   de afuera, cada tecla espera tu sí (ver agent.c). La extensión vuelve a
   revisar que la ventana sea la misma justo antes de oprimir. */
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "keys.h"
#include "linux/acciones.h"
#include "linux/gnome.h"
#include "tools.h"
#include "util.h"

#define MAX_TIMES 20
#define MAX_TABS 30

uint32_t lx_keysym(unsigned short vk)
{
    switch (vk) {
    case VK_BACK: return XK_BackSpace;
    case VK_TAB: return XK_Tab;
    case VK_RETURN: return XK_Return;
    case VK_SHIFT: return XK_Shift_L;
    case VK_CONTROL: return XK_Control_L;
    case VK_MENU: return XK_Alt_L;
    case VK_CAPITAL: return XK_Caps_Lock;
    case VK_ESCAPE: return XK_Escape;
    case VK_SPACE: return ' ';
    case VK_PRIOR: return XK_Prior;
    case VK_NEXT: return XK_Next;
    case VK_END: return XK_End;
    case VK_HOME: return XK_Home;
    case VK_LEFT: return XK_Left;
    case VK_UP: return XK_Up;
    case VK_RIGHT: return XK_Right;
    case VK_DOWN: return XK_Down;
    case VK_SNAPSHOT: return XK_Print;
    case VK_INSERT: return XK_Insert;
    case VK_DELETE: return XK_Delete;
    case VK_LWIN: return XK_Super_L;
    case VK_APPS: return XK_Menu;
    /* Los signos: GNOME busca en qué tecla están con tu distribución (en la
       latinoamericana el + tiene tecla propia; en la de EE. UU. va con Shift). */
    case VK_OEM_PLUS: return '+';
    case VK_OEM_MINUS: return '-';
    case VK_OEM_PERIOD: return '.';
    case VK_OEM_COMMA: return ',';
    default: break;
    }
    if (vk >= VK_F1 && vk <= VK_F12) return XK_F1 + (uint32_t)(vk - VK_F1);
    if (vk >= 'A' && vk <= 'Z') return (uint32_t)(vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9') return vk;
    return 0;
}

static bool has_key(const KeyCombo *k, unsigned short vk)
{
    for (int i = 0; i < k->n; i++)
        if (k->vk[i] == vk) return true;
    return false;
}

static bool has_fkey(const KeyCombo *k, int n)
{
    return has_key(k, (unsigned short)(VK_F1 + n - 1));
}

/* Las que son de GNOME y no de la ventana de enfrente: la tecla Windows
   (Super), Alt+Tab, Alt+F1/F2 («Ejecutar»), Impr Pant (captura), Ctrl+Alt+T
   (terminal) y Ctrl+Alt+flechas (escritorios). */
static bool is_global(const KeyCombo *k)
{
    bool ctrl = has_key(k, VK_CONTROL), alt = has_key(k, VK_MENU);
    return has_key(k, VK_LWIN) || has_key(k, VK_SNAPSHOT) || (alt && has_key(k, VK_TAB)) ||
           (alt && (has_fkey(k, 1) || has_fkey(k, 2))) ||
           (ctrl && alt &&
            (has_key(k, 'T') || has_key(k, VK_LEFT) || has_key(k, VK_RIGHT) || has_key(k, VK_UP) || has_key(k, VK_DOWN)));
}

/* Enfoca la ventana (si dijo cuál) o deja enfrente la tuya. NULL si se pudo;
   si no, qué decir (heap). */
static char *go_to_window(const char *ventana, const char *nothing_done)
{
    if (str_is_blank(ventana)) {
        app_yield_focus();
        return NULL;
    }
    bool ok;
    char *r = focus_window_by_title(ventana, &ok);
    if (ok) {
        free(r);
        return NULL;
    }
    char *m = str_printf("%s %s", r, nothing_done);
    free(r);
    return m;
}

char *tool_presionar_teclas(const cJSON *a)
{
    const char *teclas = arg_str(a, "teclas");
    int veces = arg_int(a, "veces", 1);
    if (veces < 1) veces = 1;
    if (veces > MAX_TIMES) veces = MAX_TIMES;
    if (str_is_blank(teclas)) return xstrdup("No me dijiste qué tecla oprimir.");
    KeyCombo k;
    if (!keys_parse(teclas, &k))
        return str_printf("No sé qué tecla es «%s», así que no oprimí nada. Se dicen como «control zeta», «alt tab», "
                          "«F5» o «flecha abajo».",
                          teclas);
    char *desc = keys_describe(&k);
    char *r = NULL;
    if (has_key(&k, VK_CONTROL) && has_key(&k, VK_MENU) && has_key(&k, VK_DELETE)) {
        r = xstrdup("No oprimí nada: en GNOME Ctrl+Alt+Supr abre «Cerrar sesión», y eso mejor lo haces tú.");
    } else if (k.n == 2 && has_key(&k, VK_LWIN) && has_key(&k, 'L')) {
        r = session_lock() ? xstrdup("Bloqueé la PC.") : xstrdup("No pude bloquear la PC.");
    }
    if (!r) r = go_to_window(arg_str(a, "ventana"), "No oprimí nada.");
    if (r) {
        free(desc);
        return r;
    }
    uint32_t syms[KEYS_MAX];
    int n = 0;
    for (int i = 0; i < k.n; i++) {
        uint32_t s = lx_keysym(k.vk[i]);
        if (s) syms[n++] = s;
    }
    bool global = is_global(&k);
    GnomeWindow w = {0};
    bool shell = false;
    GnomeStatus st = global ? GN_OK : gnome_focused(&w, &shell);
    if (n != k.n) {
        r = str_printf("No oprimí nada: no sé cómo oprimir %s en Linux.", desc);
    } else if (st) {
        r = xstrdup(gnome_status_message(st));
    } else if (!global && (!w.id || shell)) {
        r = str_printf("No oprimí %s: no hay ninguna ventana tuya enfrente.", desc);
    } else if (k.has_enter && w.terminal) {
        r = xstrdup("No oprimí nada: por seguridad no le doy Enter a terminales (Terminal, Consola y parecidas), ahí "
                    "ejecutaría un comando.");
    } else {
        char *status = NULL;
        st = gnome_press_keys(global ? 0 : w.id, syms, n, veces, &status);
        const char *app = global ? NULL : lx_app_name(&w, NULL);
        char times[24] = "";
        if (veces > 1) snprintf(times, sizeof times, " %d veces", veces);
        if (st) r = xstrdup(gnome_status_message(st));
        else if (!strcmp(status, "ok"))
            r = app ? str_printf("Oprimí %s%s en %s.", desc, times, app) : str_printf("Oprimí %s%s.", desc, times);
        else if (str_starts_with(status, "focus:"))
            r = str_printf("Oprimí %s %d de %d veces y me detuve: cambiaste de ventana.", desc, atoi(status + 6), veces);
        else r = lx_refusal(status, "No oprimí nada");
        free(status);
    }
    gnome_window_clear(&w);
    free(desc);
    return r;
}

/* ------------------------------------------------------------ pestañas --- */

typedef struct {
    uint64_t id;
    bool lost; /* cambiaste de ventana mientras buscaba */
    int moved; /* cuántas veces pasó a la siguiente */
} TabCtx;

static char *tab_title(void *ctx)
{
    TabCtx *c = ctx;
    GnomeWindow w;
    bool shell = false;
    char *t = NULL;
    if (gnome_focused(&w, &shell) == GN_OK && w.id == c->id && !shell) t = xstrdup(w.title);
    else c->lost = true;
    gnome_window_clear(&w);
    return t;
}

static void tab_next(void *ctx)
{
    TabCtx *c = ctx;
    const uint32_t k[] = {XK_Control_L, XK_Tab};
    char *st = NULL;
    if (gnome_press_keys(c->id, k, 2, 1, &st) == GN_OK && !strcmp(st, "ok")) c->moved++;
    free(st);
    Sleep(180); /* el título cambia cuando la pestaña ya se mostró */
}

char *tool_ir_a_pestana(const cJSON *a)
{
    const char *titulo = arg_str(a, "titulo");
    if (str_is_blank(titulo)) return xstrdup("No me dijiste a qué pestaña ir.");
    char *r = go_to_window(arg_str(a, "ventana"), "No cambié de pestaña.");
    if (r) return r;
    GnomeWindow w;
    bool shell = false;
    GnomeStatus st = gnome_focused(&w, &shell);
    if (st) return xstrdup(gnome_status_message(st));
    if (!w.id || shell) {
        gnome_window_clear(&w);
        return xstrdup("No hay ninguna ventana tuya enfrente donde buscar la pestaña.");
    }
    const char *where = lx_app_name(&w, "la ventana de enfrente");
    TabCtx c = {.id = w.id};
    int steps = keys_find_tab(titulo, tab_title, tab_next, &c, MAX_TABS);
    /* Los títulos no se repiten aquí: los pone cada página. */
    if (steps == 0) r = str_printf("Ya estabas en la pestaña de «%s».", titulo);
    else if (steps > 0) r = str_printf("Cambié a la pestaña de «%s» en %s.", titulo, where);
    else if (c.lost) r = xstrdup("No encontré la pestaña: cambiaste de ventana mientras la buscaba y me detuve.");
    else if (c.moved >= MAX_TABS)
        r = str_printf("No encontré una pestaña que diga «%s»: pasé por %d y me detuve.", titulo, MAX_TABS);
    else r = str_printf("No encontré una pestaña que diga «%s» en %s: las revisé todas.", titulo, where);
    gnome_window_clear(&w);
    return r;
}

/* ------------------------------------------------------ subir archivos --- */

/* Espera a que la app que se acaba de abrir tenga su ventana enfrente. */
static bool wait_for_window(const char *ventana, int ms)
{
    ULONGLONG until = GetTickCount64() + (ULONGLONG)ms;
    while (GetTickCount64() < until) {
        Sleep(500);
        bool ok;
        free(focus_window_by_title(ventana, &ok));
        if (ok) return true;
    }
    return false;
}

static char *paste_into(const char *name, const char *ventana, bool enviar, const char *kind)
{
    GnomeWindow w;
    bool shell = false;
    GnomeStatus st = gnome_focused(&w, &shell);
    const char *where = lx_app_name(&w, ventana);
    char *r;
    if (st) {
        r = str_printf("Copié «%s», pero no lo pegué: %s", name, gnome_status_message(st));
    } else if (!w.id || lx_runs_commands(&w, shell)) {
        /* Archivos, terminales, la vista de actividades: ahí pegar copia el
           archivo y Enter lo abriría. */
        r = str_printf("Copié «%s», pero no lo pegué: %s no es un chat. Pégalo tú con Ctrl+V donde quieras.", name,
                       w.id ? where : "lo de enfrente");
    } else {
        Sleep(250);
        const uint32_t paste[] = {XK_Control_L, 'v'};
        char *s = NULL;
        st = gnome_press_keys(w.id, paste, 2, 1, &s);
        bool pasted = !st && !strcmp(s, "ok");
        free(s);
        if (!pasted) {
            r = str_printf("Copié «%s», pero no lo pegué: cambiaste de ventana. Pégalo tú con Ctrl+V.", name);
        } else if (enviar) {
            /* El chat tarda en mostrar el archivo antes de poder mandarlo. */
            Sleep(900);
            const uint32_t enter = XK_Return;
            GnomeWindow now;
            bool shell_now = false;
            bool same = gnome_focused(&now, &shell_now) == GN_OK && now.id == w.id && !lx_runs_commands(&now, shell_now);
            gnome_window_clear(&now);
            s = NULL;
            if (same) st = gnome_press_keys(w.id, &enter, 1, 1, &s);
            if (same && !st && !strcmp(s, "ok"))
                r = str_printf("Pegué «%s» en %s y le di Enter para mandarlo. No veo la pantalla: si ahí no había un "
                               "chat abierto, no se subió.",
                               name, where);
            else
                r = str_printf("Pegué «%s» en %s, pero no le di Enter: mientras tanto quedó enfrente otra ventana.",
                               name, where);
            free(s);
        } else {
            r = str_printf("Pegué «%s» en %s, sin mandarlo. No veo la pantalla: revisa que quedó en el chat correcto "
                           "y dale Enter.",
                           name, where);
        }
        if (pasted && kind && strcmp(kind, "image")) {
            char *more = str_printf("%s En Linux algunas apps solo aceptan pegar imágenes: si no apareció el archivo, "
                                    "arrástralo tú.",
                                    r);
            free(r);
            r = more;
        }
    }
    gnome_window_clear(&w);
    return r;
}

char *tool_subir_archivo(const cJSON *a)
{
    const char *ruta = arg_str(a, "ruta"), *ventana = arg_str(a, "ventana");
    bool enviar = arg_bool(a, "enviar");
    if (str_is_blank(ruta)) return xstrdup("No me dijiste qué archivo subir.");
    if (str_is_blank(ventana)) return xstrdup("No me dijiste a dónde subirlo (Discord, WhatsApp…).");
    wchar_t *path = resolve_path(ruta);
    char *p = wide_to_utf8(path);
    char *full = realpath(p, NULL);
    char *r = NULL, *kind = NULL;
    if (path_is_off_limits(path)) {
        r = xstrdup("No subo archivos de rutas de red ni de las carpetas donde Sokari guarda su configuración y su "
                    "memoria.");
    } else if (!full || !file_exists(path)) {
        r = str_printf("No encontré el archivo «%s». Búscalo primero con buscar_archivo y usa la ruta completa.", ruta);
    } else {
        GnomeStatus st = gnome_clipboard_set_file(full, &kind);
        if (st) r = xstrdup(gnome_status_message(st));
    }
    char *name = wide_to_utf8(path_basename(path));
    free(full);
    free(p);
    free(path);
    if (!r) {
        bool ok;
        free(focus_window_by_title(ventana, &ok));
        if (!ok) {
            /* No está abierta: se abre y se le da tiempo de cargar. */
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "name", ventana);
            char *opened = tool_open_app(o);
            cJSON_Delete(o);
            bool started = !str_starts_with(opened, "No ") && !str_starts_with(opened, "Por seguridad") &&
                           !str_starts_with(opened, "Para eso");
            free(opened);
            ok = started && wait_for_window(ventana, 12000);
            if (!ok)
                r = str_printf(started ? "Copié «%s», pero %s no terminó de abrir a tiempo: pégalo tú con Ctrl+V en el "
                                         "chat."
                                       : "Copié «%s», pero no encontré %s abierto ni instalado: pégalo tú con Ctrl+V "
                                         "donde quieras.",
                               name, ventana);
        }
        if (!r) r = paste_into(name, ventana, enviar, kind);
    }
    free(kind);
    free(name);
    return r;
}
