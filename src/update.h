#ifndef SOKARI_UPDATE_H
#define SOKARI_UPDATE_H

#include <stdbool.h>

#include "third_party/cJSON.h"

void update_cleanup_old(void);
void update_start_background(void);

/* Para el botón de Configuración: revisa ya y devuelve un mensaje para
   mostrar (heap). Si hay versión nueva, la baja e instala en segundo plano. */
char *update_check_now(void);

/* De la lista "assets" de un release, el Sokari.exe (en Linux, el Sokari.deb
   o el Sokari.rpm, según el sistema), o NULL. */
const cJSON *update_pick_asset(const cJSON *assets);

#endif
