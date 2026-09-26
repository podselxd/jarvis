/* Arrancar con tu sesión (un .desktop en ~/.config/autostart, como lo hace
   GNOME con «Aplicaciones de inicio») y el atajo Ctrl+Alt+J para hablarle
   (un atajo propio de GNOME que corre «sokari --hablar»). */
#include <windows.h>

#include <gio/gio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "autostart.h"
#include "config.h"
#include "linux/linux.h"
#include "log.h"
#include "util.h"

#define DESKTOP_ID "io.github.podselxd.Sokari.desktop"
#define KEYS_SCHEMA "org.gnome.settings-daemon.plugins.media-keys"
#define KEY_SCHEMA "org.gnome.settings-daemon.plugins.media-keys.custom-keybinding"
#define KEY_PATH "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/sokari/"
#define HOTKEY "<Control><Alt>j"

/* Cómo se llama a este Sokari: "sokari" si el del PATH es este mismo; si no
   (uno compilado a mano), su ruta completa (heap). */
char *linux_self_command(void)
{
    char *self = realpath("/proc/self/exe", NULL);
    char *in_path = g_find_program_in_path("sokari");
    char *real = in_path ? realpath(in_path, NULL) : NULL;
    char *r = self && real && !strcmp(self, real) ? xstrdup("sokari") : xstrdup(self ? self : "sokari");
    free(self);
    free(real);
    g_free(in_path);
    return r;
}

static char *autostart_file(void)
{
    char *p = g_build_filename(g_get_user_config_dir(), "autostart", DESKTOP_ID, NULL);
    char *r = xstrdup(p);
    g_free(p);
    return r;
}

bool autostart_is_enabled(void)
{
    char *p = autostart_file();
    bool on = g_file_test(p, G_FILE_TEST_EXISTS);
    free(p);
    return on;
}

bool autostart_set(bool enable)
{
    char *p = autostart_file();
    bool ok;
    if (!enable) {
        ok = unlink(p) == 0 || !g_file_test(p, G_FILE_TEST_EXISTS);
    } else {
        char *dir = g_path_get_dirname(p);
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
        char *cmd = linux_self_command();
        char *text = str_printf("[Desktop Entry]\nType=Application\nName=Sokari\nComment=Tu asistente de voz\n"
                                "Exec=%s --autostart\nIcon=io.github.podselxd.Sokari\nTerminal=false\n"
                                "X-GNOME-Autostart-enabled=true\n",
                                cmd);
        ok = g_file_set_contents(p, text, -1, NULL);
        free(text);
        free(cmd);
    }
    free(p);
    return ok;
}

/* Si la ruta de Sokari cambió (lo reinstalaste en otro lado), el arranque
   apunta al de ahora. */
void autostart_refresh(void)
{
    if (!autostart_is_enabled()) return;
    char *p = autostart_file(), *text = NULL, *cmd = linux_self_command();
    char *want = str_printf("Exec=%s --autostart\n", cmd);
    if (g_file_get_contents(p, &text, NULL, NULL) && !strstr(text, want)) autostart_set(true);
    g_free(text);
    free(want);
    free(cmd);
    free(p);
}

LaunchKind launch_kind(bool has_key, bool from_autostart, bool updated)
{
    if (!has_key) return LAUNCH_FIRST_RUN;
    return from_autostart || updated ? LAUNCH_DIRECT : LAUNCH_HOME;
}

/* ------------------------------------------------------------- atajo --- */

static GSettingsSchema *lookup_schema(const char *id)
{
    GSettingsSchemaSource *src = g_settings_schema_source_get_default();
    return src ? g_settings_schema_source_lookup(src, id, TRUE) : NULL;
}

bool linux_hotkey_ensure(void)
{
    GSettingsSchema *keys = lookup_schema(KEYS_SCHEMA), *one = lookup_schema(KEY_SCHEMA);
    bool ok = false;
    if (!keys || !one) {
        if (keys) g_settings_schema_unref(keys);
        if (one) g_settings_schema_unref(one);
        return false; /* no es GNOME */
    }
    g_settings_schema_unref(keys);
    g_settings_schema_unref(one);
    GSettings *media = g_settings_new(KEYS_SCHEMA);
    gchar **paths = g_settings_get_strv(media, "custom-keybindings");
    bool ours = false, taken = false;
    char *cmd = linux_self_command();
    char *want = str_printf("%s --hablar", cmd);
    for (int i = 0; paths[i]; i++) {
        GSettings *k = g_settings_new_with_path(KEY_SCHEMA, paths[i]);
        char *binding = g_settings_get_string(k, "binding");
        if (!strcmp(paths[i], KEY_PATH)) {
            ours = true;
            char *have = g_settings_get_string(k, "command");
            /* Sokari se movió de lugar: el atajo apunta al de ahora. */
            if (strcmp(have, want)) g_settings_set_string(k, "command", want);
            g_free(have);
        } else if (!g_ascii_strcasecmp(binding, HOTKEY)) {
            taken = true;
        }
        g_free(binding);
        g_object_unref(k);
    }
    if (ours) {
        ok = true;
    } else if (taken) {
        log_msg("Atajo: Ctrl+Alt+J ya es de otro atajo tuyo en GNOME; no lo cambio.");
    } else {
        GSettings *k = g_settings_new_with_path(KEY_SCHEMA, KEY_PATH);
        g_settings_set_string(k, "name", "Sokari: hablarle");
        g_settings_set_string(k, "command", want);
        g_settings_set_string(k, "binding", HOTKEY);
        g_object_unref(k);
        guint n = g_strv_length(paths);
        const gchar **more = g_new0(const gchar *, n + 2);
        for (guint i = 0; i < n; i++) more[i] = paths[i];
        more[n] = KEY_PATH;
        ok = g_settings_set_strv(media, "custom-keybindings", more);
        g_free(more);
        log_msg(ok ? "Atajo: Ctrl+Alt+J ahora le habla a Sokari (en Configuración de GNOME → Teclado → Atajos)."
                   : "Atajo: no pude agregar Ctrl+Alt+J.");
    }
    g_settings_sync();
    free(want);
    free(cmd);
    g_strfreev(paths);
    g_object_unref(media);
    return ok;
}
