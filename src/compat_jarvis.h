#ifndef SOKARI_COMPAT_JARVIS_H
#define SOKARI_COMPAT_JARVIS_H

/* Todo lo necesario para pasar de la versión anterior (que se llamaba
   Jarvis) a Sokari vive aquí y solo aquí. Es temporal: cuando tus equipos ya
   corran Sokari, este archivo y su .c se borran. */

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

/* Encabezado con el que la versión anterior manda y espera el secreto de la
   malla (tus otras PCs que todavía no se actualizan). */
#define COMPAT_JARVIS_MESH_HEADER "X-Jarvis-Secret"

/* Prefijo de las claves de config.env y nombre del log de entonces. */
#define COMPAT_JARVIS_KEY_PREFIX "JARVIS_"
#define COMPAT_JARVIS_LOG_NAME L"jarvis.log"

/* Carpeta de la versión anterior dentro de base (%LOCALAPPDATA% o el
   Escritorio). */
wchar_t *compat_jarvis_dir(const wchar_t *base);

/* La primera vez copia (no mueve) la configuración y la memoria de las
   carpetas anteriores a las de Sokari. Va antes de config_load. Devuelve true
   si copió algo. */
bool compat_jarvis_migrate_data(void);

/* Las claves de config.env de antes (JARVIS_...) se leen como las de ahora
   (SOKARI_...). Devuelve key tal cual si no es de antes. */
const char *compat_jarvis_config_key(const char *key, char *buf, size_t n);

/* Si la versión anterior está abierta, ofrece cerrarla (al prender la PC no
   pregunta). Si se puede seguir, además se queda con su mutex para que la
   anterior no arranque mientras Sokari corre. Devuelve false si este proceso
   tiene que salir. */
bool compat_jarvis_take_over(bool from_autostart);

/* Inicio con Windows: la entrada de la versión anterior pasa a Sokari (si
   apuntaba a este exe, o si en este arranque se copiaron sus datos), y el
   acceso directo de la versión en Python se cambia por la entrada de Sokari. */
void compat_jarvis_migrate_autostart(bool data_migrated);

#endif
