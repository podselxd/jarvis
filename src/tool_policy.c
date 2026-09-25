/* Qué herramientas meten a la conversación texto que no dijo quien habla, y
   qué acciones, mientras ese texto siga en la conversación, solo se hacen con
   un "sí" de voz. Va aparte de tools.c para poder probarlo sin Groq. */
#include <stdlib.h>
#include <string.h>

#include "tools.h"
#include "util.h"

static bool in_list(const char *name, const char *const *list, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(name, list[i])) return true;
    return false;
}

/* Internet, archivos, portapapeles, títulos de ventanas (las pestañas los
   pone cada página) y lo que responde otra PC: cualquiera puede traer
   instrucciones escondidas para el modelo. */
bool tool_brings_outside_text(const char *name)
{
    static const char *const OUTSIDE[] = {"web_search",        "leer_pagina",  "read_file",
                                          "list_files",        "buscar_archivo", "leer_portapapeles",
                                          "list_windows",      "gestionar_dispositivo"};
    return name && in_list(name, OUTSIDE, sizeof OUTSIDE / sizeof *OUTSIDE);
}

bool tool_needs_confirmation(const char *name, const cJSON *args)
{
    if (!name) return false;
    if (!strcmp(name, "open_app")) return open_app_targets_file(args);
    if (!strcmp(name, "type_text")) return arg_bool(args, "enviar");
    /* Darse permiso para todo es delicado; quitárselo, nunca. */
    if (!strcmp(name, "cambiar_permisos")) return arg_bool(args, "acceso_completo");
    /* run_macro también: un comando guardado puede traer type_text con enviar. */
    static const char *const ALWAYS[] = {"mover_archivo", "borrar_archivo",        "create_macro",
                                         "run_macro",     "registrar_dispositivo", "gestionar_dispositivo"};
    return in_list(name, ALWAYS, sizeof ALWAYS / sizeof *ALWAYS);
}

/* Recorta para que la pregunta de confirmación se pueda decir en voz alta. */
static char *clip(const char *s)
{
    char *t = str_trim(s);
    size_t cut = utf8_truncate_len(t, 90);
    if (!t[cut]) return t;
    char *r = str_printf("%.*s…", (int)cut, t);
    free(t);
    return r;
}

/* La descripción la arma el código, no el modelo: si el modelo está engañado,
   igual se escucha qué se va a hacer de verdad. */
char *tool_describe_action(const char *name, const cJSON *args)
{
    char *a = clip(arg_str(args, !strcmp(name, "open_app")                 ? "name"
                                 : !strcmp(name, "type_text")              ? "texto"
                                 : !strcmp(name, "mover_archivo")          ? "origen"
                                 : !strcmp(name, "borrar_archivo")         ? "ruta"
                                 : !strcmp(name, "create_macro")           ? "name"
                                 : !strcmp(name, "run_macro")              ? "name"
                                 : !strcmp(name, "registrar_dispositivo")  ? "nombre"
                                 : !strcmp(name, "gestionar_dispositivo")  ? "nombre"
                                                                           : ""));
    char *r;
    if (!strcmp(name, "open_app")) {
        r = str_printf("abrir %s", a);
    } else if (!strcmp(name, "type_text")) {
        char *v = clip(arg_str(args, "ventana"));
        r = *v ? str_printf("escribir «%s» en %s y enviarlo", a, v) : str_printf("escribir «%s» y enviarlo", a);
        free(v);
    } else if (!strcmp(name, "mover_archivo")) {
        char *d = clip(arg_str(args, "destino_carpeta"));
        r = str_printf("mover %s a %s", a, d);
        free(d);
    } else if (!strcmp(name, "borrar_archivo")) {
        r = str_printf("mandar a la papelera %s", a);
    } else if (!strcmp(name, "create_macro")) {
        r = str_printf("crear el comando «%s»", a);
    } else if (!strcmp(name, "run_macro")) {
        r = str_printf("ejecutar el comando «%s»", a);
    } else if (!strcmp(name, "registrar_dispositivo")) {
        char *h = clip(arg_str(args, "host"));
        r = str_printf("registrar el dispositivo %s en %s", a, h);
        free(h);
    } else if (!strcmp(name, "gestionar_dispositivo")) {
        char *c = clip(arg_str(args, "comando"));
        r = str_printf("mandarle a %s la orden «%s»", a, c);
        free(c);
    } else if (!strcmp(name, "cambiar_permisos")) {
        r = xstrdup(arg_bool(args, "acceso_completo") ? "darme acceso completo (ya no preguntarte nada salvo antes de borrar)"
                                                       : "volver a pedirte permiso");
    } else {
        r = str_printf("usar %s", name);
    }
    free(a);
    return r;
}
