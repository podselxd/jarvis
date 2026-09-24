#ifndef JARVIS_MESH_H
#define JARVIS_MESH_H

#include <stdbool.h>

/* Procesa un comando de texto que llegó por la malla. Lo implementa el
   agente; devuelve la respuesta (heap) o NULL si Jarvis está ocupado. */
typedef char *(*MeshHandler)(const char *comando);

bool mesh_start(MeshHandler handler);
void mesh_stop(void);
char *mesh_tailscale_ip(void);
bool tailscale_installed(void);

#endif
