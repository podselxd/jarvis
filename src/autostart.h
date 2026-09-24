#ifndef JARVIS_AUTOSTART_H
#define JARVIS_AUTOSTART_H

#include <stdbool.h>
#include <wchar.h>

#define AUTOSTART_FLAG L"--autostart"

bool autostart_is_enabled(void);
bool autostart_set(bool enable);
void autostart_migrate_legacy(void);
/* Si la clave Run de una versión anterior apunta a este mismo .exe sin
   --autostart, se la agrega (así al prender la PC no sale la ventana de Inicio). */
void autostart_refresh(void);
/* La clave Run "Jarvis" pasa a "Sokari" (ver autostart.c). */
void autostart_migrate_from_jarvis(bool data_migrated);
bool autostart_value_points_to(const wchar_t *value, const wchar_t *exe);

/* Lo que va en la clave Run: "ruta\Sokari.exe" --autostart. */
wchar_t *autostart_command(const wchar_t *exe);
bool autostart_needs_refresh(const wchar_t *value, const wchar_t *exe);

/* Cómo arranca Sokari: sin API key, la primera configuración; con Windows o
   después de actualizarse, directo; abierto a mano, la ventana de Inicio. */
typedef enum { LAUNCH_FIRST_RUN, LAUNCH_HOME, LAUNCH_DIRECT } LaunchKind;
LaunchKind launch_kind(bool has_key, bool from_autostart, bool updated);

#endif
