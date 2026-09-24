#ifndef JARVIS_UPDATE_H
#define JARVIS_UPDATE_H

#include <stdbool.h>

void update_cleanup_old(void);
void update_start_background(void);

/* Para el botón de Configuración: revisa ya y devuelve un mensaje para
   mostrar (heap). Si hay versión nueva, la baja e instala en segundo plano. */
char *update_check_now(void);

#endif
