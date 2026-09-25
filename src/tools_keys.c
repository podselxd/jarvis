/* Teclas y atajos para cualquier app, cambiar de pestaña y subir un archivo a
   un chat, con el teclado de Windows (SendInput). Sokari no ve la pantalla:
   dice en qué app oprimió, nunca que "ya quedó" algo que no puede saber. Y no
   repite títulos de ventanas o pestañas (los pone cada página, y podrían
   traer instrucciones para el modelo): nombra la app por su programa.
   Con teclas se puede hacer casi todo, así que si en la conversación hay algo
   de afuera, cada tecla que pida el modelo espera tu sí (ver agent.c). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "keys.h"
#include "tools.h"
#include "util.h"

#define MAX_TIMES 20
#define MAX_TABS 30

static bool is_extended(WORD vk)
{
    switch (vk) {
    case VK_UP:
    case VK_DOWN:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_INSERT:
    case VK_DELETE:
    case VK_LWIN:
    case VK_APPS:
        return true;
    default:
        return false;
    }
}

static void key_event(INPUT *in, WORD vk, bool up)
{
    memset(in, 0, sizeof *in);
    in->type = INPUT_KEYBOARD;
    in->ki.wVk = vk;
    in->ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    /* Sin "extendida", Shift+flechas se vuelven las del teclado numérico y no
       seleccionan. */
    in->ki.dwFlags = (is_extended(vk) ? KEYEVENTF_EXTENDEDKEY : 0) | (up ? KEYEVENTF_KEYUP : 0);
}

/* Todas abajo en orden y arriba al revés, como las oprime una persona. */
static void press(const KeyCombo *k)
{
    INPUT in[2 * KEYS_MAX];
    int n = 0;
    for (int i = 0; i < k->n; i++) key_event(&in[n++], k->vk[i], false);
    for (int i = k->n - 1; i >= 0; i--) key_event(&in[n++], k->vk[i], true);
    SendInput((UINT)n, in, sizeof(INPUT));
}

static bool has_key(const KeyCombo *k, WORD vk)
{
    for (int i = 0; i < k->n; i++)
        if (k->vk[i] == vk) return true;
    return false;
}

/* Las que son de Windows y no de la ventana de enfrente. */
static bool is_global(const KeyCombo *k)
{
    return has_key(k, VK_LWIN) || (has_key(k, VK_MENU) && has_key(k, VK_TAB)) ||
           (has_key(k, VK_CONTROL) && has_key(k, VK_SHIFT) && has_key(k, VK_ESCAPE));
}

static bool token_elevated(HANDLE token)
{
    TOKEN_ELEVATION e = {0};
    DWORD n = 0;
    return GetTokenInformation(token, TokenElevation, &e, sizeof e, &n) && e.TokenIsElevated;
}

/* Windows no deja que un programa normal le oprima teclas a uno que corre como
   administrador (el Administrador de tareas, por ejemplo): las tira sin
   avisar. Mejor decirlo que decir que se oprimieron. */
static bool runs_as_admin_over_us(HWND h)
{
    HANDLE t;
    if (!h || !OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return false;
    bool me = token_elevated(t);
    CloseHandle(t);
    if (me) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return false;
    bool elevated;
    if (OpenProcessToken(p, TOKEN_QUERY, &t)) {
        elevated = token_elevated(t);
        CloseHandle(t);
    } else {
        elevated = GetLastError() == ERROR_ACCESS_DENIED;
    }
    CloseHandle(p);
    return elevated;
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
        r = xstrdup("No oprimí nada: Windows no deja que un programa oprima Ctrl+Alt+Supr, eso solo lo puedes hacer tú.");
    } else if (k.n == 2 && has_key(&k, VK_LWIN) && has_key(&k, 'L')) {
        /* Windows+L tampoco se puede oprimir desde un programa, pero bloquear sí. */
        r = LockWorkStation() ? xstrdup("Bloqueé la PC.") : xstrdup("No pude bloquear la PC.");
    }
    if (!r) r = go_to_window(arg_str(a, "ventana"), "No oprimí nada.");
    if (r) {
        free(desc);
        return r;
    }
    HWND fg = GetForegroundWindow();
    bool global = is_global(&k);
    char *app = global ? NULL : window_app_name(fg);
    if (!global && (!fg || app_is_own_window(fg))) {
        r = str_printf("No oprimí %s: no hay ninguna ventana tuya enfrente.", desc);
    } else if (k.has_enter && foreground_is_terminal()) {
        r = xstrdup("No oprimí nada: por seguridad no le doy Enter a terminales (cmd, PowerShell y parecidas), ahí "
                    "ejecutaría un comando.");
    } else if (!global && runs_as_admin_over_us(fg)) {
        r = str_printf("No oprimí nada: %s corre como administrador y Windows no deja que otro programa le oprima "
                       "teclas.",
                       app ? app : "la ventana de enfrente");
    } else {
        for (int i = 0; i < veces; i++) {
            if (i) Sleep(60);
            press(&k);
        }
        char times[24] = "";
        if (veces > 1) snprintf(times, sizeof times, " %d veces", veces);
        r = app ? str_printf("Oprimí %s%s en %s.", desc, times, app) : str_printf("Oprimí %s%s.", desc, times);
    }
    free(app);
    free(desc);
    return r;
}


/* ------------------------------------------------------------ pestañas --- */

typedef struct {
    HWND h;
    bool lost;  /* cambiaste de ventana mientras buscaba */
    int moved;  /* cuántas veces pasó a la siguiente */
} TabCtx;

static char *tab_title(void *ctx)
{
    TabCtx *c = ctx;
    if (GetForegroundWindow() != c->h) {
        c->lost = true;
        return NULL;
    }
    wchar_t t[512] = L"";
    GetWindowTextW(c->h, t, 512);
    return wide_to_utf8(t);
}

static void tab_next(void *ctx)
{
    TabCtx *c = ctx;
    KeyCombo k = {.vk = {VK_CONTROL, VK_TAB}, .n = 2};
    press(&k);
    c->moved++;
    Sleep(180); /* el título cambia cuando la pestaña ya se mostró */
}

char *tool_ir_a_pestana(const cJSON *a)
{
    const char *titulo = arg_str(a, "titulo");
    if (str_is_blank(titulo)) return xstrdup("No me dijiste a qué pestaña ir.");
    char *r = go_to_window(arg_str(a, "ventana"), "No cambié de pestaña.");
    if (r) return r;
    HWND fg = GetForegroundWindow();
    if (!fg || app_is_own_window(fg)) return xstrdup("No hay ninguna ventana tuya enfrente donde buscar la pestaña.");
    char *app = window_app_name(fg);
    const char *where = app ? app : "la ventana de enfrente";
    if (runs_as_admin_over_us(fg)) {
        r = str_printf("No cambié de pestaña: %s corre como administrador y Windows no deja que otro programa le "
                       "oprima teclas.",
                       where);
    } else {
        TabCtx c = {.h = fg};
        int steps = keys_find_tab(titulo, tab_title, tab_next, &c, MAX_TABS);
        /* Los títulos no se repiten aquí: los pone cada página. */
        if (steps == 0) r = str_printf("Ya estabas en la pestaña de «%s».", titulo);
        else if (steps > 0) r = str_printf("Cambié a la pestaña de «%s» en %s.", titulo, where);
        else if (c.lost) r = xstrdup("No encontré la pestaña: cambiaste de ventana mientras la buscaba y me detuve.");
        else if (c.moved >= MAX_TABS)
            r = str_printf("No encontré una pestaña que diga «%s»: pasé por %d y me detuve.", titulo, MAX_TABS);
        else r = str_printf("No encontré una pestaña que diga «%s» en %s: las revisé todas.", titulo, where);
    }
    free(app);
    return r;
}

/* ------------------------------------------------------ subir archivos --- */

bool clipboard_set_file(const wchar_t *path)
{
    size_t len = wcslen(path);
    /* DROPFILES y después las rutas, cada una con su NUL y un NUL más al final. */
    HGLOBAL drop = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + sizeof(wchar_t) * (len + 2));
    HGLOBAL effect = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    bool ok = false;
    if (drop && effect) {
        DROPFILES *df = GlobalLock(drop);
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        memcpy((char *)df + sizeof(DROPFILES), path, sizeof(wchar_t) * len);
        GlobalUnlock(drop);
        /* "Copiar", no "cortar": el archivo se queda donde está. */
        *(DWORD *)GlobalLock(effect) = DROPEFFECT_COPY;
        GlobalUnlock(effect);
        if (open_clipboard()) {
            EmptyClipboard();
            ok = SetClipboardData(CF_HDROP, drop) != NULL;
            if (ok) drop = NULL;
            if (ok && SetClipboardData(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT), effect)) effect = NULL;
            CloseClipboard();
        }
    }
    if (drop) GlobalFree(drop);
    if (effect) GlobalFree(effect);
    return ok;
}

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

char *tool_subir_archivo(const cJSON *a)
{
    const char *ruta = arg_str(a, "ruta"), *ventana = arg_str(a, "ventana");
    bool enviar = arg_bool(a, "enviar");
    if (str_is_blank(ruta)) return xstrdup("No me dijiste qué archivo subir.");
    if (str_is_blank(ventana)) return xstrdup("No me dijiste a dónde subirlo (Discord, WhatsApp…).");
    wchar_t *path = resolve_path(ruta);
    DWORD n = GetFullPathNameW(path, 0, NULL, NULL);
    if (n) {
        wchar_t *full = xmalloc(sizeof(wchar_t) * n);
        DWORD got = GetFullPathNameW(path, n, full, NULL);
        if (got && got < n) {
            free(path);
            path = full;
        } else {
            free(full);
        }
    }
    char *r = NULL;
    if (path_is_off_limits(path)) {
        r = xstrdup("No subo archivos de rutas de red ni de las carpetas donde Sokari guarda su configuración y su "
                    "memoria.");
    } else if (!file_exists(path)) {
        r = str_printf("No encontré el archivo «%s». Búscalo primero con buscar_archivo y usa la ruta completa.", ruta);
    } else if (!clipboard_set_file(path)) {
        r = xstrdup("No pude copiar el archivo al portapapeles (otra app lo está usando).");
    }
    char *name = wide_to_utf8(path_basename(path));
    free(path);
    if (r) {
        free(name);
        return r;
    }
    bool ok;
    char *f = focus_window_by_title(ventana, &ok);
    free(f);
    if (!ok) {
        /* No está abierta: se abre y se le da tiempo de cargar. */
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", ventana);
        char *opened = tool_open_app(o);
        cJSON_Delete(o);
        bool started = !str_starts_with(opened, "No ") && !str_starts_with(opened, "Por seguridad");
        free(opened);
        ok = started && wait_for_window(ventana, 12000);
        if (!ok) {
            r = str_printf(started ? "Copié «%s», pero %s no terminó de abrir a tiempo: pégalo tú con Ctrl+V en el chat."
                                   : "Copié «%s», pero no encontré %s abierto ni instalado: pégalo tú con Ctrl+V donde "
                                     "quieras.",
                           name, ventana);
            free(name);
            return r;
        }
    }
    HWND fg = GetForegroundWindow();
    char *app = window_app_name(fg);
    const char *where = app ? app : ventana;
    if (window_runs_commands(fg)) {
        /* El Explorador, el escritorio, «Ejecutar», terminales: ahí pegar copia
           el archivo y Enter lo abriría. */
        r = str_printf("Copié «%s», pero no lo pegué: %s no es un chat. Pégalo tú con Ctrl+V donde quieras.", name,
                       where);
    } else if (runs_as_admin_over_us(fg)) {
        r = str_printf("Copié «%s», pero %s corre como administrador y Windows no deja que le pegue nada. Pégalo tú "
                       "con Ctrl+V.",
                       name, where);
    } else {
        Sleep(250);
        KeyCombo paste = {.vk = {VK_CONTROL, 'V'}, .n = 2};
        press(&paste);
        /* El chat tarda en mostrar el archivo antes de poder mandarlo. */
        if (enviar) Sleep(900);
        if (enviar && window_runs_commands(GetForegroundWindow())) {
            r = str_printf("Pegué «%s» en %s, pero no le di Enter: mientras tanto quedó enfrente una ventana donde "
                           "Enter abre o ejecuta cosas.",
                           name, where);
        } else if (enviar) {
            KeyCombo enter = {.vk = {VK_RETURN}, .n = 1};
            press(&enter);
            r = str_printf("Pegué «%s» en %s y le di Enter para mandarlo. No veo la pantalla: si ahí no había un chat "
                           "abierto, no se subió.",
                           name, where);
        } else {
            r = str_printf("Pegué «%s» en %s, sin mandarlo. No veo la pantalla: revisa que quedó en el chat correcto "
                           "y dale Enter.",
                           name, where);
        }
    }
    free(app);
    free(name);
    return r;
}
