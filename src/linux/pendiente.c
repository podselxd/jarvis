/* Lo que todavía no está hecho para Linux: cada herramienta lo dice claro en
   vez de fingir que lo hizo. La malla (tus otras PCs) llega en la parte 5. */
#include <stdlib.h>

#include "mesh.h"
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

PENDIENTE(tool_registrar_dispositivo)
PENDIENTE(tool_gestionar_dispositivo)

bool mesh_start(MeshHandler handler)
{
    (void)handler;
    return false;
}

void mesh_stop(void) {}
