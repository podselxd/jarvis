/* Lo que todavía no está hecho para Linux en esta parte del trabajo: cada
   herramienta lo dice claro en vez de fingir que lo hizo. Se va vaciando en
   las siguientes partes (voz, acciones, malla). */
#include <stdlib.h>

#include "sounds.h"
#include "tools.h"
#include "util.h"

static char *not_yet(void)
{
    return xstrdup("No lo hice: eso todavía no funciona en la versión de Linux.");
}

#define PENDIENTE(fn) \
    char *fn(const cJSON *a) \
    { \
        (void)a; \
        return not_yet(); \
    }

PENDIENTE(tool_open_app)
PENDIENTE(tool_control_media)
PENDIENTE(tool_control_desktop)
PENDIENTE(tool_focus_window)
PENDIENTE(tool_list_windows)
PENDIENTE(tool_type_text)
PENDIENTE(tool_leer_portapapeles)
PENDIENTE(tool_copiar_portapapeles)
PENDIENTE(tool_info_sistema)
PENDIENTE(tool_list_files)
PENDIENTE(tool_read_file)
PENDIENTE(tool_buscar_archivo)
PENDIENTE(tool_mover_archivo)
PENDIENTE(tool_borrar_archivo)
PENDIENTE(tool_presionar_teclas)
PENDIENTE(tool_ir_a_pestana)
PENDIENTE(tool_subir_archivo)
PENDIENTE(tool_registrar_dispositivo)
PENDIENTE(tool_gestionar_dispositivo)

bool open_url(const char *url, const char *browser)
{
    (void)url, (void)browser;
    return false;
}

void sound_search_start(void) {}
void sound_search_stop(void) {}
