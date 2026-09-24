#ifndef JARVIS_AUTOSTART_H
#define JARVIS_AUTOSTART_H

#include <stdbool.h>

bool autostart_is_enabled(void);
bool autostart_set(bool enable);
void autostart_migrate_legacy(void);

#endif
