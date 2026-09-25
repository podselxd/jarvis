#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "config.h"
#include "log.h"
#include "tools.h"
#include "util.h"

/* GUIDs de Core Audio y del shell, definidos acá para no depender de que la
   libuuid de MinGW los traiga todos. */
static const GUID JV_CLSID_MMDeviceEnumerator = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID JV_IID_IMMDeviceEnumerator = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID JV_IID_IAudioEndpointVolume = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};
static const GUID JV_FOLDERID_AppsFolder = {0x1e87508d, 0x89c2, 0x42f0, {0x8a, 0x7e, 0x64, 0x5a, 0x0f, 0x50, 0xca, 0x58}};
static const GUID JV_IID_IShellItem = {0x43826d1e, 0xe718, 0x42ee, {0xbc, 0x55, 0xa1, 0xe2, 0x61, 0xc3, 0x7b, 0xfe}};
static const GUID JV_IID_IEnumShellItems = {0x70629033, 0xe363, 0x4a28, {0xa5, 0x67, 0x0d, 0xb7, 0x80, 0x06, 0xe6, 0xd7}};
static const GUID JV_BHID_EnumItems = {0x94f60519, 0x2850, 0x4924, {0xaa, 0x5a, 0xd1, 0x5e, 0x84, 0x86, 0x80, 0x39}};

typedef struct {
    const char *alias;
    const char *target;
} Alias;

static const Alias APP_ALIASES[] = {
    {"chrome", "chrome"},        {"google", "google.com"},    {"youtube", "youtube.com"}, {"bloc de notas", "notepad"},
    {"notas", "notepad"},        {"calculadora", "calc"},     {"explorador", "explorer"}, {"archivos", "explorer"},
    {"explorer", "explorer"},    {"file explorer", "explorer"}, {"explorador de archivos", "explorer"},
    {"este equipo", "explorer"}, {"mi pc", "explorer"},       {"mis archivos", "explorer"},
    {"word", "winword"},         {"excel", "excel"},          {"spotify", "spotify"},
};

static bool shell_open(const wchar_t *target, const wchar_t *params)
{
    SHELLEXECUTEINFOW sei = {sizeof sei};
    sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = target;
    sei.lpParameters = params;
    sei.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != 0;
}

static bool looks_like_url(const char *s)
{
    if (str_starts_with(s, "http://") || str_starts_with(s, "https://") || str_starts_with(s, "www.")) return true;
    if (strchr(s, ' ') || strchr(s, '\\')) return false;
    const char *dot = strrchr(s, '.');
    if (!dot || dot == s || strlen(dot + 1) < 2) return false;
    static const char *tlds[] = {"com", "org", "net", "io", "mx", "es", "ar", "co", "tv", "gg", "dev", "app", "ai", "edu", "gov"};
    for (size_t i = 0; i < sizeof tlds / sizeof *tlds; i++) {
        char *end = str_lower(dot + 1);
        char *slash = strchr(end, '/');
        if (slash) *slash = 0;
        bool m = !strcmp(end, tlds[i]);
        free(end);
        if (m) return true;
    }
    return false;
}

/* "esquema:" que no sea una letra de unidad (C:) ni http/https: ms-settings:,
   search-ms:, file:, shell:... abren cosas que no son apps ni páginas. */
static bool has_other_scheme(const char *s)
{
    const char *p = s;
    if (!isalpha((unsigned char)*p)) return false;
    while (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.') p++;
    if (*p != ':') return false;
    size_t n = (size_t)(p - s);
    if (n == 1) return false;
    return !(n == 4 && !_strnicmp(s, "http", 4)) && !(n == 5 && !_strnicmp(s, "https", 5));
}

/* Lo que Windows ejecuta en vez de abrir en un visor. AssocIsDangerous trae la
   lista del sistema; la propia es por si en alguna versión falta alguno. */
static const wchar_t *DANGEROUS_EXT[] = {
    L".exe", L".com", L".bat", L".cmd", L".scr", L".pif", L".cpl", L".msi", L".msp", L".msc", L".vbs",
    L".vbe", L".js", L".jse", L".wsf", L".wsh", L".ws", L".ps1", L".psm1", L".psd1", L".hta", L".jar",
    L".lnk", L".url", L".reg", L".inf", L".scf", L".application", L".appref-ms", L".appx", L".appxbundle",
    L".msix", L".msixbundle", L".appinstaller", L".settingcontent-ms", L".library-ms", L".search-ms",
    L".searchconnector-ms", L".diagcab", L".chm", L".iso", L".img", L".vhd", L".vhdx", L".xll", L".gadget",
    L".py", L".pyw", L".dll", L".sys", L".ocx",
};

bool open_target_is_dangerous(const wchar_t *path)
{
    const wchar_t *ext = PathFindExtensionW(path);
    if (!*ext) return false;
    if (AssocIsDangerous(ext)) return true;
    for (size_t i = 0; i < sizeof DANGEROUS_EXT / sizeof *DANGEROUS_EXT; i++)
        if (!_wcsicmp(ext, DANGEROUS_EXT[i])) return true;
    return false;
}

/* Minúsculas, sin acentos ni símbolos: "Spotify Premium™" -> "spotifypremium". */
static char *normalize_name(const char *s)
{
    wchar_t *w = utf8_to_wide(s);
    int n = FoldStringW(MAP_COMPOSITE, w, -1, NULL, 0);
    wchar_t *folded = xmalloc(sizeof(wchar_t) * (size_t)(n > 0 ? n : 1));
    if (n <= 0 || !FoldStringW(MAP_COMPOSITE, w, -1, folded, n)) wcscpy(folded, w);
    free(w);
    StrBuf sb;
    sb_init(&sb);
    for (wchar_t *p = folded; *p; p++) {
        wchar_t c = *p;
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) sb_append_char(&sb, (char)c);
    }
    free(folded);
    char *r = sb_steal(&sb);
    return r ? r : xstrdup("");
}

static int edit_distance(const char *a, const char *b)
{
    size_t n = strlen(a), m = strlen(b);
    if (n > 60 || m > 60) return 99;
    int prev[61], cur[61];
    for (size_t j = 0; j <= m; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= n; i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= m; j++) {
            int cost = a[i - 1] == b[j - 1] ? 0 : 1;
            int v = prev[j] + 1;
            if (cur[j - 1] + 1 < v) v = cur[j - 1] + 1;
            if (prev[j - 1] + cost < v) v = prev[j - 1] + cost;
            cur[j] = v;
        }
        memcpy(prev, cur, sizeof(int) * (m + 1));
    }
    return prev[m];
}

typedef struct {
    wchar_t *parsing;
    char *display;
    int score;
    int distance;
} AppMatch;

/* Recorre la carpeta virtual "Aplicaciones" de Windows (lo mismo que "Todas
   las apps" del menú Inicio, incluidas las de la Store): así "abre discord"
   funciona aunque Discord no esté en el PATH, que era lo que fallaba antes. */
static int find_installed_apps(const char *query, AppMatch *best, AppMatch *similar, int max_similar)
{
    char *q = normalize_name(query);
    if (strlen(q) < 2) {
        free(q);
        return 0;
    }
    IShellItem *apps = NULL;
    IEnumShellItems *en = NULL;
    int n_similar = 0;
    best->score = 0;
    if (FAILED(SHGetKnownFolderItem(&JV_FOLDERID_AppsFolder, KF_FLAG_DEFAULT, NULL, &JV_IID_IShellItem, (void **)&apps)))
        goto done;
    if (FAILED(IShellItem_BindToHandler(apps, NULL, &JV_BHID_EnumItems, &JV_IID_IEnumShellItems, (void **)&en)))
        goto done;
    IShellItem *item = NULL;
    while (IEnumShellItems_Next(en, 1, &item, NULL) == S_OK) {
        LPWSTR disp = NULL, parse = NULL;
        if (SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_NORMALDISPLAY, &disp)) &&
            SUCCEEDED(IShellItem_GetDisplayName(item, SIGDN_PARENTRELATIVEPARSING, &parse))) {
            char *d = wide_to_utf8(disp);
            char *nd = normalize_name(d);
            int score = 0;
            if (!strcmp(nd, q)) score = 100;
            else if (str_starts_with(nd, q)) score = 80;
            else if (strstr(nd, q)) score = 60;
            if (score > best->score || (score && score == best->score && strlen(d) < strlen(best->display))) {
                free(best->display);
                free(best->parsing);
                best->display = xstrdup(d);
                best->parsing = xwcsdup(parse);
                best->score = score;
            }
            int dist = edit_distance(nd, q);
            if (!score && dist <= 3 + (int)strlen(q) / 4) {
                int slot = n_similar < max_similar ? n_similar++ : -1;
                if (slot < 0) {
                    for (int k = 0; k < max_similar; k++)
                        if (similar[k].distance > dist) slot = k;
                }
                if (slot >= 0 && (slot >= n_similar - 1 || similar[slot].distance > dist)) {
                    free(similar[slot].display);
                    similar[slot].display = xstrdup(d);
                    similar[slot].distance = dist;
                }
            }
            free(nd);
            free(d);
        }
        CoTaskMemFree(disp);
        CoTaskMemFree(parse);
        IShellItem_Release(item);
    }
done:
    if (en) IEnumShellItems_Release(en);
    if (apps) IShellItem_Release(apps);
    free(q);
    return n_similar;
}

/* ¿open_app apunta a una ruta (archivo o carpeta) y no a una app o página? */
bool open_app_targets_file(const cJSON *a)
{
    char *name = str_trim(arg_str(a, "name"));
    bool file = false;
    if (!looks_like_url(name)) {
        wchar_t *w = utf8_to_wide(name);
        wchar_t *e = expand_env(w);
        file = wcschr(e, L'\\') || wcschr(e, L'/');
        free(e);
        free(w);
    }
    free(name);
    return file;
}

static bool is_listable(HWND h);
static bool force_foreground(HWND h);

static const char *find_ci(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    for (; *hay; hay++)
        if (!_strnicmp(hay, needle, n)) return hay;
    return NULL;
}

/* ¿El título trae ese nombre como palabra completa? ("Word" sí en
   "Documento1 - Word", no en "Password"). */
static bool title_has_word(const char *title, const char *name)
{
    size_t n = strlen(name);
    for (const char *p = title; (p = find_ci(p, name)); p++) {
        bool before = p == title || !isalnum((unsigned char)p[-1]);
        bool after = !isalnum((unsigned char)p[n]);
        if (before && after) return true;
    }
    return false;
}

typedef struct {
    const char *name;
    HWND found;
} OpenFind;

static BOOL CALLBACK find_open_window(HWND h, LPARAM lp)
{
    OpenFind *f = (OpenFind *)lp;
    int len = GetWindowTextLengthW(h);
    if (len <= 0 || !is_listable(h)) return TRUE;
    wchar_t *w = xmalloc(sizeof(wchar_t) * (size_t)(len + 1));
    GetWindowTextW(h, w, len + 1);
    char *t = wide_to_utf8(w);
    free(w);
    if (title_has_word(t, f->name)) f->found = h;
    free(t);
    return f->found ? FALSE : TRUE;
}

/* "Abre Opera" con Opera ya abierto: se trae al frente en vez de abrir otra
   ventana. */
static char *bring_open_app(const char *name)
{
    if (strlen(name) < 3) return NULL;
    OpenFind f = {.name = name};
    EnumWindows(find_open_window, (LPARAM)&f);
    if (!f.found) return NULL;
    app_yield_focus();
    wchar_t title[256];
    GetWindowTextW(f.found, title, 256);
    char *t = wide_to_utf8(title);
    char *r = force_foreground(f.found) ? str_printf("Ya estaba abierto: traje al frente «%s».", t) : NULL;
    free(t);
    return r;
}

char *tool_open_app(const cJSON *a)
{
    char *name = str_trim(arg_str(a, "name"));
    if (!*name) {
        free(name);
        return xstrdup("No me dijiste qué abrir.");
    }
    char *low = str_lower(name);
    char *result = NULL;
    const char *target = name;
    for (size_t i = 0; i < sizeof APP_ALIASES / sizeof *APP_ALIASES; i++)
        if (!strcmp(low, APP_ALIASES[i].alias)) target = APP_ALIASES[i].target;
    bool is_alias = target != name;
    bool is_path = false;

    if (has_other_scheme(target)) {
        free(low);
        free(name);
        return xstrdup("Por seguridad solo abro apps, carpetas, archivos y páginas http o https.");
    }
    wchar_t *folder = known_folder_alias(low);
    if (folder) {
        if (shell_open(folder, NULL)) result = str_printf("Abrí %s.", name);
        free(folder);
    } else if (!strcmp(low, "navegador") || !strcmp(low, "el navegador") || !strcmp(low, "browser")) {
        /* El navegador que tengas de predeterminado, no uno fijo. */
        wchar_t exe[MAX_PATH];
        DWORD n = MAX_PATH;
        if (SUCCEEDED(AssocQueryStringW(ASSOCF_NONE, ASSOCSTR_EXECUTABLE, L"http", L"open", exe, &n)) &&
            shell_open(exe, NULL))
            result = xstrdup("Abrí tu navegador.");
    } else if (looks_like_url(target)) {
        char *url = str_starts_with(target, "http") ? xstrdup(target) : str_printf("https://%s", target);
        wchar_t *w = utf8_to_wide(url);
        if (shell_open(w, NULL)) result = str_printf("Abrí %s en el navegador.", url);
        free(w);
        free(url);
    }
    if (!result && !strchr(target, '\\') && !strchr(target, '/') && strcmp(target, "explorer"))
        result = bring_open_app(low);
    if (!result) {
        wchar_t *w = utf8_to_wide(target);
        wchar_t *expanded = expand_env(w);
        is_path = wcschr(expanded, L'\\') || wcschr(expanded, L'/');
        if (is_path) {
            /* GetFullPathNameW también quita puntos y espacios del final:
               "virus.exe." se abriría como virus.exe. */
            wchar_t full[MAX_PATH * 2];
            DWORD got = GetFullPathNameW(expanded, MAX_PATH * 2, full, NULL);
            bool network = (expanded[0] == L'\\' || expanded[0] == L'/') && (expanded[1] == L'\\' || expanded[1] == L'/');
            if (network || !got || got >= MAX_PATH * 2) {
                result = xstrdup("Por seguridad no abro rutas de red.");
            } else if (dir_exists(full)) {
                if (shell_open(full, NULL)) result = str_printf("Abrí %s.", name);
            } else if (file_exists(full)) {
                if (open_target_is_dangerous(full))
                    result = xstrdup("Por seguridad no abro programas ni scripts sueltos (.exe, .bat, accesos directos...). "
                                     "Dime el nombre de la app y la busco en el menú Inicio.");
                else if (shell_open(full, NULL))
                    result = str_printf("Abrí %s.", name);
            }
        } else if (is_alias && shell_open(expanded, NULL)) {
            /* Solo los alias fijos de arriba se abren por nombre directo; cualquier
               otro nombre se busca en el menú Inicio (así "cmd" o "mshta" sueltos
               no ejecutan lo que haya en el PATH). */
            result = str_printf("Abrí %s.", name);
        }
        free(expanded);
        free(w);
    }
    if (!result && !is_path) {
        AppMatch best = {0};
        AppMatch similar[3] = {{0}};
        int ns = find_installed_apps(name, &best, similar, 3);
        if (best.score && best.parsing) {
            wchar_t *shell = xmalloc(sizeof(wchar_t) * (wcslen(best.parsing) + 32));
            swprintf(shell, wcslen(best.parsing) + 32, L"shell:AppsFolder\\%ls", best.parsing);
            if (shell_open(shell, NULL) || shell_open(best.parsing, NULL)) result = str_printf("Abrí %s.", best.display);
            free(shell);
        }
        if (!result) {
            StrBuf sb;
            sb_init(&sb);
            sb_appendf(&sb, "No encontré ninguna app llamada '%s'.", name);
            if (ns) {
                sb_append(&sb, " Parecidas instaladas: ");
                for (int i = 0; i < ns; i++) sb_appendf(&sb, "%s%s", i ? ", " : "", similar[i].display);
                sb_append(&sb, ". Si era alguna de esas, vuelve a llamar a open_app con ese nombre exacto.");
            }
            result = sb_steal(&sb);
        }
        free(best.display);
        free(best.parsing);
        for (int i = 0; i < 3; i++) free(similar[i].display);
    }
    if (!result) result = str_printf("No encontré '%s'.", name);
    free(low);
    free(name);
    return result;
}

static void send_keys(const WORD *down, int n)
{
    INPUT in[16];
    int k = 0;
    for (int i = 0; i < n && k < 16; i++) {
        memset(&in[k], 0, sizeof in[k]);
        in[k].type = INPUT_KEYBOARD;
        in[k].ki.wVk = down[i];
        k++;
    }
    for (int i = n - 1; i >= 0 && k < 16; i--) {
        memset(&in[k], 0, sizeof in[k]);
        in[k].type = INPUT_KEYBOARD;
        in[k].ki.wVk = down[i];
        in[k].ki.dwFlags = KEYEVENTF_KEYUP;
        k++;
    }
    SendInput((UINT)k, in, sizeof(INPUT));
}

static bool set_system_volume(int level)
{
    IMMDeviceEnumerator *en = NULL;
    IMMDevice *dev = NULL;
    IAudioEndpointVolume *vol = NULL;
    bool ok = false;
    if (SUCCEEDED(CoCreateInstance(&JV_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &JV_IID_IMMDeviceEnumerator,
                                   (void **)&en)) &&
        SUCCEEDED(IMMDeviceEnumerator_GetDefaultAudioEndpoint(en, eRender, eMultimedia, &dev)) &&
        SUCCEEDED(IMMDevice_Activate(dev, &JV_IID_IAudioEndpointVolume, CLSCTX_ALL, NULL, (void **)&vol))) {
        ok = SUCCEEDED(IAudioEndpointVolume_SetMasterVolumeLevelScalar(vol, (float)level / 100.0f, NULL));
        if (level > 0) IAudioEndpointVolume_SetMute(vol, FALSE, NULL);
    }
    if (vol) IAudioEndpointVolume_Release(vol);
    if (dev) IMMDevice_Release(dev);
    if (en) IMMDeviceEnumerator_Release(en);
    return ok;
}

char *tool_control_media(const cJSON *a)
{
    const char *action = arg_str(a, "action");
    static const struct {
        const char *name;
        WORD vk;
    } keys[] = {
        {"volume_up", VK_VOLUME_UP},           {"volume_down", VK_VOLUME_DOWN},   {"mute", VK_VOLUME_MUTE},
        {"play_pause", VK_MEDIA_PLAY_PAUSE},   {"next_track", VK_MEDIA_NEXT_TRACK}, {"previous_track", VK_MEDIA_PREV_TRACK},
    };
    if (!strcmp(action, "set_volume")) {
        int lvl = arg_int(a, "nivel", -1);
        if (lvl < 0 || lvl > 100) return xstrdup("Dime un nivel de volumen entre 0 y 100.");
        return set_system_volume(lvl) ? str_printf("Listo, volumen al %d%%.", lvl)
                                      : xstrdup("No pude cambiar el volumen del sistema.");
    }
    for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) {
        if (!strcmp(action, keys[i].name)) {
            send_keys(&keys[i].vk, 1);
            return xstrdup("Listo.");
        }
    }
    return xstrdup("Acción de media no reconocida.");
}

static bool is_listable(HWND h);

/* Tu ventana de enfrente: la que tiene el foco o, si es la de Sokari, la
   primera tuya debajo de ella. */
static HWND front_user_window(void)
{
    app_yield_focus();
    HWND h = GetForegroundWindow();
    if (h && is_listable(h) && GetWindowTextLengthW(h) > 0 && !IsIconic(h)) return h;
    for (HWND w = GetTopWindow(NULL); w; w = GetWindow(w, GW_HWNDNEXT))
        if (is_listable(w) && GetWindowTextLengthW(w) > 0 && !IsIconic(w)) return w;
    return NULL;
}

static char *window_action(const char *action)
{
    HWND h = front_user_window();
    if (!h) return xstrdup("No encontré ninguna ventana tuya enfrente.");
    wchar_t title[160];
    GetWindowTextW(h, title, 160);
    char *t = wide_to_utf8(title);
    char *r;
    if (!strcmp(action, "minimize")) {
        ShowWindowAsync(h, SW_MINIMIZE);
        r = str_printf("Minimicé «%s».", t);
    } else if (!strcmp(action, "maximize")) {
        ShowWindowAsync(h, SW_MAXIMIZE);
        r = str_printf("Maximicé «%s».", t);
    } else {
        /* Como darle a la X: si tiene algo sin guardar, la app misma pregunta. */
        PostMessageW(h, WM_CLOSE, 0, 0);
        r = str_printf("Cerré «%s».", t);
    }
    free(t);
    return r;
}

char *tool_control_desktop(const cJSON *a)
{
    const char *action = arg_str(a, "action");
    if (!strcmp(action, "lock")) {
        return LockWorkStation() ? xstrdup("Listo.") : xstrdup("No pude bloquear la PC.");
    }
    if (!strcmp(action, "minimize") || !strcmp(action, "maximize") || !strcmp(action, "close_window"))
        return window_action(action);
    WORD combo[3];
    int n = 0;
    if (!strcmp(action, "show_desktop")) {
        combo[0] = VK_LWIN, combo[1] = 'D', n = 2;
    } else if (!strcmp(action, "switch_window")) {
        combo[0] = VK_MENU, combo[1] = VK_TAB, n = 2;
    } else if (!strcmp(action, "minimize_all")) {
        combo[0] = VK_LWIN, combo[1] = 'M', n = 2;
    } else if (!strcmp(action, "fullscreen")) {
        combo[0] = 'F', n = 1;
    } else if (!strcmp(action, "close_tab")) {
        combo[0] = VK_CONTROL, combo[1] = 'W', n = 2;
    }
    if (!n) return xstrdup("Acción de escritorio no reconocida.");
    app_yield_focus();
    send_keys(combo, n);
    return xstrdup("Listo.");
}

typedef struct {
    const char *needle;
    HWND found;
    StrBuf *list;
} EnumCtx;

static bool is_listable(HWND h)
{
    if (!IsWindowVisible(h) || app_is_own_window(h)) return false;
    if (GetWindow(h, GW_OWNER) && !(GetWindowLongW(h, GWL_EXSTYLE) & WS_EX_APPWINDOW)) return false;
    BOOL cloaked = FALSE;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
    if (cloaked) return false;
    wchar_t cls[64];
    GetClassNameW(h, cls, 64);
    return wcscmp(cls, L"Progman") && wcscmp(cls, L"WorkerW") && wcscmp(cls, L"Shell_TrayWnd");
}

static BOOL CALLBACK enum_windows(HWND h, LPARAM lp)
{
    EnumCtx *c = (EnumCtx *)lp;
    if (!is_listable(h)) return TRUE;
    int len = GetWindowTextLengthW(h);
    if (len <= 0) return TRUE;
    wchar_t *title = xmalloc(sizeof(wchar_t) * (size_t)(len + 1));
    GetWindowTextW(h, title, len + 1);
    char *t = wide_to_utf8(title);
    free(title);
    BOOL cont = TRUE;
    if (c->list) {
        char *trimmed = str_trim(t);
        if (*trimmed) sb_appendf(c->list, "%s%s", c->list->len ? "\n" : "", trimmed);
        free(trimmed);
    } else if (str_contains_ci(t, c->needle)) {
        c->found = h;
        cont = FALSE;
    }
    free(t);
    return cont;
}

/* SetForegroundWindow desde un proceso en segundo plano Windows lo bloquea
   sin avisar; el workaround estándar es "tomar prestado" el input del hilo
   que tiene el foco (y una pulsación de Alt, que Windows cuenta como
   interacción del usuario). */
static bool force_foreground(HWND h)
{
    if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
    HWND fg = GetForegroundWindow();
    DWORD fg_thread = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    DWORD me = GetCurrentThreadId();
    bool attached = fg_thread && fg_thread != me && AttachThreadInput(me, fg_thread, TRUE);
    INPUT alt[2] = {{0}};
    alt[0].type = alt[1].type = INPUT_KEYBOARD;
    alt[0].ki.wVk = alt[1].ki.wVk = VK_MENU;
    alt[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, alt, sizeof(INPUT));
    BringWindowToTop(h);
    SetForegroundWindow(h);
    SetFocus(h);
    if (attached) AttachThreadInput(me, fg_thread, FALSE);
    Sleep(60);
    return GetForegroundWindow() == h;
}

char *focus_window_by_title(const char *needle, bool *ok)
{
    *ok = false;
    char *n = str_trim(needle);
    if (!*n) {
        free(n);
        return xstrdup("No dijiste qué ventana buscar.");
    }
    EnumCtx c = {.needle = n};
    EnumWindows(enum_windows, (LPARAM)&c);
    char *r;
    if (!c.found) {
        r = str_printf("No encontré ninguna ventana con '%s' abierta.", n);
    } else {
        app_yield_focus();
        wchar_t title[512];
        GetWindowTextW(c.found, title, 512);
        char *t = wide_to_utf8(title);
        if (force_foreground(c.found)) {
            *ok = true;
            r = str_printf("Enfoqué: %s", t);
        } else {
            r = str_printf("Encontré %s pero Windows no me dejó traerla al frente.", t);
        }
        free(t);
    }
    free(n);
    return r;
}

char *tool_focus_window(const cJSON *a)
{
    bool ok;
    return focus_window_by_title(arg_str(a, "title_contains"), &ok);
}

char *tool_list_windows(const cJSON *a)
{
    StrBuf sb;
    sb_init(&sb);
    EnumCtx c = {.list = &sb};
    EnumWindows(enum_windows, (LPARAM)&c);
    if (!sb.len) {
        sb_free(&sb);
        return xstrdup("No encontré ninguna ventana abierta.");
    }
    return sb_steal(&sb);
}

/* Terminales: ahí lo que escribe Sokari se ejecuta como comando (con Enter,
   y en cmd hasta con el Shift+Enter de los saltos de línea). Se reconocen por
   la clase de la ventana o por el programa. */
static const wchar_t *TERMINAL_CLASSES[] = {L"ConsoleWindowClass", L"CASCADIA_HOSTING_WINDOW_CLASS",
                                            L"PseudoConsoleWindow", L"mintty", L"VirtualConsoleClass",
                                            L"PuTTY", L"KiTTY"};
static const wchar_t *TERMINAL_EXES[] = {
    L"cmd.exe",       L"powershell.exe", L"pwsh.exe",     L"powershell_ise.exe", L"WindowsTerminal.exe",
    L"OpenConsole.exe", L"conhost.exe",  L"wsl.exe",      L"wslhost.exe",        L"bash.exe",
    L"mintty.exe",    L"alacritty.exe",  L"wezterm-gui.exe", L"putty.exe",       L"kitty.exe",
    L"Hyper.exe",     L"Tabby.exe",      L"ConEmu.exe",   L"ConEmu64.exe",       L"Cmder.exe",
    L"MobaXterm.exe", L"Warp.exe"};

bool is_terminal_window_info(const wchar_t *cls, const wchar_t *exe)
{
    for (size_t i = 0; cls && i < sizeof TERMINAL_CLASSES / sizeof *TERMINAL_CLASSES; i++)
        if (!_wcsicmp(cls, TERMINAL_CLASSES[i])) return true;
    const wchar_t *base = exe && *exe ? path_basename(exe) : NULL;
    for (size_t i = 0; base && i < sizeof TERMINAL_EXES / sizeof *TERMINAL_EXES; i++)
        if (!_wcsicmp(base, TERMINAL_EXES[i])) return true;
    return false;
}

static bool window_is_terminal(HWND h)
{
    if (!h) return false;
    wchar_t cls[128] = L"", exe[MAX_PATH] = L"";
    GetClassNameW(h, cls, 128);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (p) {
        DWORD n = MAX_PATH;
        if (!QueryFullProcessImageNameW(p, 0, exe, &n)) exe[0] = 0;
        CloseHandle(p);
    }
    return is_terminal_window_info(cls, exe);
}

static void type_unicode(const wchar_t *text)
{
    INPUT batch[64];
    int k = 0;
    for (const wchar_t *p = text; *p; p++) {
        if (*p == L'\r') continue;
        if (*p == L'\n') {
            /* Salto de línea como Shift+Enter: en chats agrega una línea en
               vez de mandar el mensaje, y en editores funciona igual. */
            if (k) SendInput((UINT)k, batch, sizeof(INPUT)), k = 0;
            WORD combo[2] = {VK_SHIFT, VK_RETURN};
            send_keys(combo, 2);
            continue;
        }
        for (int up = 0; up < 2; up++) {
            memset(&batch[k], 0, sizeof batch[k]);
            batch[k].type = INPUT_KEYBOARD;
            batch[k].ki.wScan = *p;
            batch[k].ki.dwFlags = KEYEVENTF_UNICODE | (up ? KEYEVENTF_KEYUP : 0);
            k++;
        }
        if (k >= 60) {
            SendInput((UINT)k, batch, sizeof(INPUT));
            k = 0;
            Sleep(8);
        }
    }
    if (k) SendInput((UINT)k, batch, sizeof(INPUT));
}

char *tool_type_text(const cJSON *a)
{
    const char *texto = arg_str(a, "texto");
    const char *ventana = arg_str(a, "ventana");
    bool enviar = arg_bool(a, "enviar");
    if (str_is_blank(texto)) return xstrdup("No me dijiste qué escribir.");
    if (!str_is_blank(ventana)) {
        bool ok;
        char *r = focus_window_by_title(ventana, &ok);
        if (!ok) return r;
        free(r);
    } else {
        app_yield_focus();
    }
    if (window_is_terminal(GetForegroundWindow()))
        return xstrdup("Por seguridad no escribo en terminales (cmd, PowerShell, Windows Terminal y parecidas): ahí el "
                       "texto se ejecuta como comando.");
    wchar_t *w = utf8_to_wide(texto);
    type_unicode(w);
    free(w);
    /* Sokari escribe a ciegas: dice dónde lo escribió, pero no puede saber si
       ahí había un chat abierto, así que no afirma que se envió. */
    wchar_t title[128] = L"";
    GetWindowTextW(GetForegroundWindow(), title, 128);
    char *where = wide_to_utf8(*title ? title : L"la ventana de enfrente");
    char *r;
    if (enviar) {
        Sleep(40);
        WORD enter = VK_RETURN;
        send_keys(&enter, 1);
        r = str_printf("Escribí el texto en «%s» y le di Enter. No veo la pantalla: si ahí no había un chat o un "
                       "cuadro de texto abierto, no se envió.",
                       where);
    } else {
        r = str_printf("Escribí el texto en «%s», sin enviarlo. No veo la pantalla: que revise que quedó donde "
                       "quería.",
                       where);
    }
    free(where);
    return r;
}

static bool open_clipboard(void)
{
    for (int i = 0; i < 10; i++) {
        if (OpenClipboard(NULL)) return true;
        Sleep(30);
    }
    return false;
}

char *tool_leer_portapapeles(const cJSON *a)
{
    if (!open_clipboard()) return xstrdup("No pude abrir el portapapeles (otra app lo está usando).");
    char *r = NULL;
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) {
        const wchar_t *w = GlobalLock(h);
        if (w) {
            char *t = wide_to_utf8(w);
            char *trimmed = str_trim(t);
            if (*trimmed) r = trimmed;
            else free(trimmed);
            free(t);
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    if (!r) return xstrdup("No hay texto en el portapapeles (puede tener una imagen u otra cosa).");
    size_t cut = utf8_truncate_len(r, 6000);
    if (cut < strlen(r)) {
        r[cut] = 0;
        char *longer = str_printf("%s\n[...se cortó aquí, el texto copiado sigue...]", r);
        free(r);
        r = longer;
    }
    return r;
}

char *tool_copiar_portapapeles(const cJSON *a)
{
    const char *texto = arg_str(a, "texto");
    if (str_is_blank(texto)) return xstrdup("No me dijiste qué copiar.");
    wchar_t *w = utf8_to_wide(texto);
    size_t bytes = (wcslen(w) + 1) * sizeof(wchar_t);
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, bytes);
    bool ok = false;
    if (g) {
        memcpy(GlobalLock(g), w, bytes);
        GlobalUnlock(g);
        if (open_clipboard()) {
            EmptyClipboard();
            ok = SetClipboardData(CF_UNICODETEXT, g) != NULL;
            CloseClipboard();
        }
        if (!ok) GlobalFree(g);
    }
    free(w);
    return ok ? xstrdup("Listo, lo copié.") : xstrdup("No pude copiar al portapapeles.");
}

static unsigned long long ft64(FILETIME f)
{
    return ((unsigned long long)f.dwHighDateTime << 32) | f.dwLowDateTime;
}

char *tool_info_sistema(const cJSON *a)
{
    StrBuf sb;
    sb_init(&sb);
    FILETIME i1, k1, u1, i2, k2, u2;
    if (GetSystemTimes(&i1, &k1, &u1)) {
        Sleep(300);
        GetSystemTimes(&i2, &k2, &u2);
        unsigned long long idle = ft64(i2) - ft64(i1);
        unsigned long long total = (ft64(k2) - ft64(k1)) + (ft64(u2) - ft64(u1));
        if (total) sb_appendf(&sb, "CPU al %.0f%%", 100.0 * (double)(total - idle) / (double)total);
    }
    MEMORYSTATUSEX mem = {sizeof mem};
    if (GlobalMemoryStatusEx(&mem)) {
        unsigned long long gb = 1ull << 30;
        sb_appendf(&sb, "%sRAM al %lu%% (%lluGB de %lluGB)", sb.len ? "; " : "", (unsigned long)mem.dwMemoryLoad,
                   (mem.ullTotalPhys - mem.ullAvailPhys) / gb, mem.ullTotalPhys / gb);
    }
    wchar_t *home = expand_env(L"%USERPROFILE%");
    ULARGE_INTEGER freeb, totalb;
    if (GetDiskFreeSpaceExW(home, NULL, &totalb, &freeb) && totalb.QuadPart) {
        double used = 100.0 * (double)(totalb.QuadPart - freeb.QuadPart) / (double)totalb.QuadPart;
        sb_appendf(&sb, "%sdisco al %.0f%% usado (%lluGB libres)", sb.len ? "; " : "", used,
                   freeb.QuadPart >> 30);
    }
    free(home);
    SYSTEM_POWER_STATUS ps;
    if (GetSystemPowerStatus(&ps) && ps.BatteryFlag != 128 && ps.BatteryLifePercent != 255)
        sb_appendf(&sb, "%sbatería al %u%% (%s)", sb.len ? "; " : "", (unsigned)ps.BatteryLifePercent,
                   ps.ACLineStatus == 1 ? "cargando" : "sin cargador");
    if (!sb.len) {
        sb_free(&sb);
        return xstrdup("No pude leer el estado del sistema.");
    }
    return sb_steal(&sb);
}

/* "Tienes permiso para todo" / "pregúntame antes". Prenderlo con texto de
   afuera en la conversación pide un sí de voz (ver tool_needs_confirmation). */
char *tool_cambiar_permisos(const cJSON *a)
{
    bool on = arg_bool(a, "acceso_completo");
    config_set_full_access(on);
    log_msg(on ? "Acceso completo prendido por voz." : "Acceso completo apagado por voz: vuelvo a pedir permiso.");
    return xstrdup(on ? "Listo: acceso completo prendido. Ya no te pregunto nada, salvo antes de borrar."
                      : "Listo: acceso completo apagado. Vuelvo a pedirte permiso antes de acciones delicadas.");
}
