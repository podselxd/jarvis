/* Dónde guarda Sokari sus cosas en Linux, según la convención de carpetas de
   freedesktop (XDG): la configuración, el registro y tus dispositivos en
   ~/.config/sokari, y la memoria, tus datos y perfiles en
   ~/.local/share/sokari. Las dos solo las puede leer tu usuario. */
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

#include "config.h"
#include "util.h"

static wchar_t *xdg_dir(const char *var, const wchar_t *fallback)
{
    const char *v = getenv(var);
    if (v && v[0] == '/') return utf8_to_wide(v); /* la especificación pide rutas absolutas */
    const char *home = getenv("HOME");
    if (!home || !*home) {
        struct passwd *pw = getpwuid(getuid());
        home = pw ? pw->pw_dir : "/tmp";
    }
    wchar_t *h = utf8_to_wide(home);
    wchar_t *r = path_join(h, fallback);
    free(h);
    return r;
}

void paths_init(void)
{
    wchar_t *config = xdg_dir("XDG_CONFIG_HOME", L".config");
    wchar_t *data = xdg_dir("XDG_DATA_HOME", L".local/share");
    g_paths.local_dir = path_join(config, L"sokari");
    g_paths.memory_dir = path_join(data, L"sokari");
    free(config);
    free(data);
    /* En Linux no hubo versión anterior (Jarvis): nada más que proteger. */
    g_paths.legacy_local_dir = NULL;
    g_paths.legacy_memory_dir = NULL;
    g_paths.config_file = path_join(g_paths.local_dir, L"config.env");
    g_paths.log_file = path_join(g_paths.local_dir, L"sokari.log");
    g_paths.sounds_dir = path_join(g_paths.local_dir, L"sounds");
    g_paths.update_dir = path_join(g_paths.local_dir, L"update");
    ensure_dir(g_paths.local_dir);
}
