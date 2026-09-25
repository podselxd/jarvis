/* Herramientas y reglas que no dependen del sistema: las usan igual Windows
   y Linux. */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "config.h"
#include "keys.h"
#include "log.h"
#include "tools.h"
#include "util.h"

bool looks_like_url(const char *s)
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
bool has_other_scheme(const char *s)
{
    const char *p = s;
    if (!isalpha((unsigned char)*p)) return false;
    while (isalnum((unsigned char)*p) || *p == '+' || *p == '-' || *p == '.') p++;
    if (*p != ':') return false;
    size_t n = (size_t)(p - s);
    if (n == 1) return false;
    return !(n == 4 && !strncasecmp(s, "http", 4)) && !(n == 5 && !strncasecmp(s, "https", 5));
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

char *tool_atajos_de_app(const cJSON *a)
{
    const char *app = arg_str(a, "app");
    const char *s = keys_shortcuts_for(app);
    if (s) return xstrdup(s);
    return str_printf("No tengo guardados atajos de «%s»; usa los que sepas de esa app. Los de Windows: %s", app,
                      keys_shortcuts_for("windows"));
}
