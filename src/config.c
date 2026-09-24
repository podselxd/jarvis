#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "util.h"

AppPaths g_paths;

static AppConfig g_cfg;
static SRWLOCK g_lock = SRWLOCK_INIT;

/* "windowed_borderless" es la esfera flotante: el nombre viene de antes de que
   existiera el modo Ventana y se queda así para leer configuraciones viejas. */
static const char *DISPLAY_KEYS[DISPLAY_MODE_COUNT] = {"fullscreen", "fullscreen_borderless", "windowed_borderless",
                                                      "windowed", "minimized"};

const char *display_mode_key(int mode)
{
    if (mode < 0 || mode >= DISPLAY_MODE_COUNT) mode = DISPLAY_FULLSCREEN_BORDERLESS;
    return DISPLAY_KEYS[mode];
}

static wchar_t *known_folder(REFKNOWNFOLDERID id)
{
    PWSTR p = NULL;
    wchar_t *r = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, NULL, &p))) r = xwcsdup(p);
    CoTaskMemFree(p);
    return r;
}

void paths_init(void)
{
    wchar_t *local = known_folder(&FOLDERID_LocalAppData);
    if (!local) local = expand_env(L"%USERPROFILE%\\AppData\\Local");
    g_paths.local_dir = path_join(local, L"Sokari");
    g_paths.legacy_local_dir = path_join(local, L"Jarvis");
    free(local);

    /* Misma regla que desde la versión en Python: %OneDrive%\Desktop\Sokari,
       o ~\Desktop\Sokari (antes ...\Jarvis). */
    wchar_t base[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"OneDrive", base, (DWORD)(sizeof base / sizeof base[0]));
    wchar_t *root = (n && n < sizeof base / sizeof base[0]) ? xwcsdup(base) : NULL;
    if (!root) root = known_folder(&FOLDERID_Profile);
    if (!root) root = expand_env(L"%USERPROFILE%");
    wchar_t *desk = path_join(root, L"Desktop");
    g_paths.memory_dir = path_join(desk, L"Sokari");
    g_paths.legacy_memory_dir = path_join(desk, L"Jarvis");
    free(desk);
    free(root);

    g_paths.config_file = path_join(g_paths.local_dir, L"config.env");
    g_paths.log_file = path_join(g_paths.local_dir, L"sokari.log");
    g_paths.sounds_dir = path_join(g_paths.local_dir, L"sounds");
    g_paths.update_dir = path_join(g_paths.local_dir, L"update");
    ensure_dir(g_paths.local_dir);
}

static void set_str(char **field, const char *value)
{
    free(*field);
    *field = value ? xstrdup(value) : xstrdup("");
}

static bool parse_bool(const char *v)
{
    return v && (!strcmp(v, "1") || !_stricmp(v, "true") || !_stricmp(v, "si") || !_stricmp(v, "yes"));
}

static void defaults(AppConfig *c)
{
    memset(c, 0, sizeof *c);
    c->groq_api_key = xstrdup("");
    c->user_name = xstrdup("");
    c->stop_word = xstrdup("");
    c->mesh_secret = xstrdup("");
    c->voice = xstrdup("");
    c->mic_name = xstrdup("");
    c->output_name = xstrdup("");
    c->display_mode = DISPLAY_FULLSCREEN_BORDERLESS;
    c->resolution = 0;
    c->volume = 100;
    c->wake_sensitivity = 67;
    c->sphere_style = 0;
    c->orb_x = c->orb_y = -1;
    c->win_x = c->win_y = c->win_w = c->win_h = -1;
    c->subtitles = true;
    c->autostart = false;
    c->mic_muted = false;
}

static void apply_kv(AppConfig *c, const char *key, const char *value)
{
    if (!strcmp(key, "GROQ_API_KEY")) set_str(&c->groq_api_key, value);
    else if (!strcmp(key, "JARVIS_USER_NAME")) set_str(&c->user_name, value);
    else if (!strcmp(key, "JARVIS_STOP_WORD")) set_str(&c->stop_word, value);
    else if (!strcmp(key, "JARVIS_MESH_SECRET")) set_str(&c->mesh_secret, value);
    else if (!strcmp(key, "JARVIS_VOICE")) set_str(&c->voice, value);
    else if (!strcmp(key, "JARVIS_MIC")) set_str(&c->mic_name, value);
    else if (!strcmp(key, "JARVIS_OUTPUT")) set_str(&c->output_name, value);
    else if (!strcmp(key, "JARVIS_DISPLAY_MODE")) {
        for (int i = 0; i < DISPLAY_MODE_COUNT; i++)
            if (!strcmp(value, DISPLAY_KEYS[i])) c->display_mode = i;
    } else if (!strcmp(key, "JARVIS_RESOLUTION")) {
        c->resolution = atoi(value);
        if (c->resolution != 720 && c->resolution != 1080 && c->resolution != 1440 && c->resolution != 2160)
            c->resolution = 0;
    } else if (!strcmp(key, "JARVIS_VOLUME")) {
        int v = atoi(value);
        c->volume = v < 0 ? 0 : v > 100 ? 100 : v;
    } else if (!strcmp(key, "JARVIS_WAKE_SENSITIVITY")) {
        int v = atoi(value);
        c->wake_sensitivity = v < 0 ? 0 : v > 100 ? 100 : v;
    } else if (!strcmp(key, "JARVIS_SPHERE_STYLE")) {
        c->sphere_style = !strcmp(value, "lineas") ? 1 : 0;
    } else if (!strcmp(key, "JARVIS_ORB_POS")) {
        if (sscanf(value, "%d,%d", &c->orb_x, &c->orb_y) != 2) c->orb_x = c->orb_y = -1;
    } else if (!strcmp(key, "JARVIS_WINDOW")) {
        if (sscanf(value, "%d,%d,%d,%d", &c->win_x, &c->win_y, &c->win_w, &c->win_h) != 4 || c->win_w <= 0 ||
            c->win_h <= 0)
            c->win_x = c->win_y = c->win_w = c->win_h = -1;
    } else if (!strcmp(key, "JARVIS_SUBTITLES")) c->subtitles = parse_bool(value);
    else if (!strcmp(key, "JARVIS_AUTOSTART")) c->autostart = parse_bool(value);
    else if (!strcmp(key, "JARVIS_MIC_MUTED")) c->mic_muted = parse_bool(value);
}

static bool load_env_file(const wchar_t *path, AppConfig *c)
{
    size_t len;
    char *text = read_file_all(path, &len);
    if (!text) return false;
    char *save = NULL;
    for (char *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *t = str_trim(line);
        char *eq = strchr(t, '=');
        if (*t && *t != '#' && eq) {
            *eq = 0;
            char *k = str_trim(t), *v = str_trim(eq + 1);
            apply_kv(c, k, v);
            free(k);
            free(v);
        }
        free(t);
    }
    free(text);
    return true;
}

void config_load(void)
{
    AcquireSRWLockExclusive(&g_lock);
    defaults(&g_cfg);
    load_env_file(g_paths.config_file, &g_cfg);
    ReleaseSRWLockExclusive(&g_lock);
}

static void put_kv(StrBuf *sb, const char *key, const char *value)
{
    char *clean = xstrdup(value ? value : "");
    for (char *p = clean; *p; p++)
        if (*p == '\r' || *p == '\n') *p = ' ';
    sb_appendf(sb, "%s=%s\n", key, clean);
    free(clean);
}

static bool save_locked(void)
{
    StrBuf sb;
    sb_init(&sb);
    sb_append(&sb, "# Configuración de Sokari. Se edita desde la ventana de Configuración.\n");
    put_kv(&sb, "GROQ_API_KEY", g_cfg.groq_api_key);
    put_kv(&sb, "JARVIS_USER_NAME", g_cfg.user_name);
    put_kv(&sb, "JARVIS_STOP_WORD", g_cfg.stop_word);
    put_kv(&sb, "JARVIS_MESH_SECRET", g_cfg.mesh_secret);
    put_kv(&sb, "JARVIS_VOICE", g_cfg.voice);
    put_kv(&sb, "JARVIS_MIC", g_cfg.mic_name);
    put_kv(&sb, "JARVIS_OUTPUT", g_cfg.output_name);
    put_kv(&sb, "JARVIS_DISPLAY_MODE", display_mode_key(g_cfg.display_mode));
    sb_appendf(&sb, "JARVIS_RESOLUTION=%d\n", g_cfg.resolution);
    sb_appendf(&sb, "JARVIS_VOLUME=%d\n", g_cfg.volume);
    sb_appendf(&sb, "JARVIS_WAKE_SENSITIVITY=%d\n", g_cfg.wake_sensitivity);
    sb_appendf(&sb, "JARVIS_SPHERE_STYLE=%s\n", g_cfg.sphere_style == 1 ? "lineas" : "puntos");
    sb_appendf(&sb, "JARVIS_ORB_POS=%d,%d\n", g_cfg.orb_x, g_cfg.orb_y);
    sb_appendf(&sb, "JARVIS_WINDOW=%d,%d,%d,%d\n", g_cfg.win_x, g_cfg.win_y, g_cfg.win_w, g_cfg.win_h);
    sb_appendf(&sb, "JARVIS_SUBTITLES=%d\n", g_cfg.subtitles ? 1 : 0);
    sb_appendf(&sb, "JARVIS_AUTOSTART=%d\n", g_cfg.autostart ? 1 : 0);
    sb_appendf(&sb, "JARVIS_MIC_MUTED=%d\n", g_cfg.mic_muted ? 1 : 0);
    ensure_dir(g_paths.local_dir);
    bool ok = write_file_atomic(g_paths.config_file, sb.data, sb.len);
    sb_free(&sb);
    if (!ok) log_msg("No pude guardar la configuración.");
    return ok;
}

bool config_save(void)
{
    AcquireSRWLockShared(&g_lock);
    bool ok = save_locked();
    ReleaseSRWLockShared(&g_lock);
    return ok;
}

static void copy_cfg(AppConfig *dst, const AppConfig *src)
{
    *dst = *src;
    dst->groq_api_key = xstrdup(src->groq_api_key);
    dst->user_name = xstrdup(src->user_name);
    dst->stop_word = xstrdup(src->stop_word);
    dst->mesh_secret = xstrdup(src->mesh_secret);
    dst->voice = xstrdup(src->voice);
    dst->mic_name = xstrdup(src->mic_name);
    dst->output_name = xstrdup(src->output_name);
}

void config_free(AppConfig *c)
{
    free(c->groq_api_key);
    free(c->user_name);
    free(c->stop_word);
    free(c->mesh_secret);
    free(c->voice);
    free(c->mic_name);
    free(c->output_name);
    memset(c, 0, sizeof *c);
}

AppConfig config_snapshot(void)
{
    AppConfig c;
    AcquireSRWLockShared(&g_lock);
    copy_cfg(&c, &g_cfg);
    ReleaseSRWLockShared(&g_lock);
    return c;
}

void config_apply(const AppConfig *c)
{
    AcquireSRWLockExclusive(&g_lock);
    AppConfig old = g_cfg;
    copy_cfg(&g_cfg, c);
    config_free(&old);
    save_locked();
    ReleaseSRWLockExclusive(&g_lock);
}

#define GETTER_STR(fn, field)                  \
    char *fn(void)                             \
    {                                          \
        AcquireSRWLockShared(&g_lock);         \
        char *r = xstrdup(g_cfg.field);        \
        ReleaseSRWLockShared(&g_lock);         \
        return r;                              \
    }

GETTER_STR(config_api_key, groq_api_key)
GETTER_STR(config_stop_word, stop_word)
GETTER_STR(config_user_name, user_name)

int config_volume(void)
{
    AcquireSRWLockShared(&g_lock);
    int v = g_cfg.volume;
    ReleaseSRWLockShared(&g_lock);
    return v;
}

float config_wake_threshold(void)
{
    AcquireSRWLockShared(&g_lock);
    int s = g_cfg.wake_sensitivity;
    ReleaseSRWLockShared(&g_lock);
    /* sensibilidad 67 ~= 0.4, el umbral que usaba la versión anterior */
    return 0.9f - 0.75f * (float)s / 100.0f;
}

bool config_mic_muted(void)
{
    AcquireSRWLockShared(&g_lock);
    bool m = g_cfg.mic_muted;
    ReleaseSRWLockShared(&g_lock);
    return m;
}

void config_set_mic_muted(bool muted)
{
    AcquireSRWLockExclusive(&g_lock);
    g_cfg.mic_muted = muted;
    save_locked();
    ReleaseSRWLockExclusive(&g_lock);
}

void config_set_orb_pos(int x, int y)
{
    AcquireSRWLockExclusive(&g_lock);
    g_cfg.orb_x = x;
    g_cfg.orb_y = y;
    save_locked();
    ReleaseSRWLockExclusive(&g_lock);
}

void config_set_window_rect(int x, int y, int w, int h)
{
    AcquireSRWLockExclusive(&g_lock);
    g_cfg.win_x = x;
    g_cfg.win_y = y;
    g_cfg.win_w = w;
    g_cfg.win_h = h;
    save_locked();
    ReleaseSRWLockExclusive(&g_lock);
}

void config_set_display_mode(int mode)
{
    if (mode < 0 || mode >= DISPLAY_MODE_COUNT) return;
    AcquireSRWLockExclusive(&g_lock);
    g_cfg.display_mode = mode;
    save_locked();
    ReleaseSRWLockExclusive(&g_lock);
}

void config_set_output(const char *name)
{
    AcquireSRWLockExclusive(&g_lock);
    set_str(&g_cfg.output_name, name);
    save_locked();
    ReleaseSRWLockExclusive(&g_lock);
}

/* Se genera una sola vez (32 bytes aleatorios) y nunca lo elige ni lo dice
   el usuario en voz alta: es lo que autentica a tus otros dispositivos. */
char *config_mesh_secret(bool create)
{
    AcquireSRWLockExclusive(&g_lock);
    if (create && !*g_cfg.mesh_secret) {
        unsigned char raw[32];
        random_bytes(raw, sizeof raw);
        free(g_cfg.mesh_secret);
        g_cfg.mesh_secret = hex_encode(raw, sizeof raw);
        save_locked();
        log_msg("Secreto de malla nuevo generado (Configuración > Dispositivos para copiarlo a tus otras PCs).");
    }
    char *r = xstrdup(g_cfg.mesh_secret);
    ReleaseSRWLockExclusive(&g_lock);
    return r;
}

static void copy_if_missing(const wchar_t *src_dir, const wchar_t *name, const wchar_t *dst_dir)
{
    wchar_t *src = path_join(src_dir, name);
    wchar_t *dst = path_join(dst_dir, name);
    if (file_exists(src) && !file_exists(dst)) {
        ensure_dir(dst_dir);
        if (CopyFileW(src, dst, TRUE)) {
            char *n = wide_to_utf8(name);
            log_msg("Migrado desde la versión anterior: %s", n);
            free(n);
        }
    }
    free(src);
    free(dst);
}

/* Copia una carpeta completa sin pisar lo que ya exista del otro lado ni
   seguir enlaces (un enlace podría apuntar a cualquier parte del disco).
   Devuelve cuántos archivos copió. */
static int copy_tree(const wchar_t *src, const wchar_t *dst, const wchar_t *const *skip, int nskip)
{
    if (!ensure_dir(dst)) return 0;
    wchar_t *pattern = path_join(src, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int copied = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        bool skipped = false;
        for (int i = 0; i < nskip && !skipped; i++) skipped = !_wcsicmp(fd.cFileName, skip[i]);
        if (skipped) continue;
        wchar_t *s = path_join(src, fd.cFileName), *d = path_join(dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) copied += copy_tree(s, d, NULL, 0);
        else if (CopyFileW(s, d, TRUE)) copied++;
        free(s);
        free(d);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return copied;
}

static bool dir_missing_or_empty(const wchar_t *dir)
{
    wchar_t *pattern = path_join(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool empty = true;
    do {
        if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) empty = false;
    } while (empty && FindNextFileW(h, &fd));
    FindClose(h);
    return empty;
}

/* Sokari antes se llamaba Jarvis. La primera vez que corre se trae la
   configuración (%LOCALAPPDATA%\Jarvis) y la memoria (Escritorio\Jarvis) a
   sus carpetas nuevas. Copia, no mueve: las de Jarvis se quedan como
   respaldo y por si vuelves a abrir la versión anterior. Devuelve true si
   copió algo. Va antes de config_load. */
bool config_migrate_from_jarvis(void)
{
    bool migrated = false;
    wchar_t *old_cfg = path_join(g_paths.legacy_local_dir, L"config.env");
    if (!file_exists(g_paths.config_file) && file_exists(old_cfg)) {
        /* El log y las descargas a medias de una actualización no hacen falta. */
        static const wchar_t *const SKIP[] = {L"jarvis.log", L"update"};
        int n = copy_tree(g_paths.legacy_local_dir, g_paths.local_dir, SKIP, 2);
        log_msg("Configuración de Jarvis copiada a la carpeta de Sokari (%d archivos).", n);
        migrated = true;
    }
    free(old_cfg);
    if (dir_missing_or_empty(g_paths.memory_dir) && !dir_missing_or_empty(g_paths.legacy_memory_dir)) {
        int n = copy_tree(g_paths.legacy_memory_dir, g_paths.memory_dir, NULL, 0);
        log_msg("Memoria de Jarvis copiada a la carpeta de Sokari (%d archivos).", n);
        migrated = true;
    }
    return migrated;
}

/* La versión en Python guardaba .env, commands.json, dispositivos.json y
   los sonidos junto al .exe (o al script). La primera vez que corre esta
   versión, los copia a %LOCALAPPDATA%\Sokari para que nada quede regado —
   sin borrar los originales. */
void config_migrate_legacy(void)
{
    bool fresh = !file_exists(g_paths.config_file);
    wchar_t *dir = exe_dir();
    wchar_t *parent = path_dirname(dir);
    const wchar_t *candidates[] = {dir, parent};

    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < 2; i++) {
        const wchar_t *d = candidates[i];
        if (fresh) {
            wchar_t *env = path_join(d, L".env");
            if (load_env_file(env, &g_cfg)) {
                log_msg("Configuración migrada desde el .env de la versión anterior.");
                fresh = false;
                save_locked();
            }
            free(env);
        }
        copy_if_missing(d, L"commands.json", g_paths.memory_dir);
        copy_if_missing(d, L"dispositivos.json", g_paths.local_dir);
        wchar_t *snd = path_join(d, L"sounds");
        const wchar_t *sounds[] = {L"activacion.mp3", L"activacion.wav", L"busqueda.mp3", L"busqueda.wav"};
        for (int s = 0; s < 4; s++) copy_if_missing(snd, sounds[s], g_paths.sounds_dir);
        free(snd);
    }
    ReleaseSRWLockExclusive(&g_lock);
    free(dir);
    free(parent);
}
