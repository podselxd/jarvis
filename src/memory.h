#ifndef JARVIS_MEMORY_H
#define JARVIS_MEMORY_H

#include <stdbool.h>
#include <wchar.h>

#include "third_party/cJSON.h"

#define DEFAULT_PROFILE "default"

/* Todo el estado compartido (perfil actual, archivos de memoria) se toca con
   este lock tomado: la voz local y los pedidos por la malla nunca se pisan. */
void state_lock(void);
bool state_try_lock(unsigned timeout_ms);
void state_unlock(void);

void memory_init(void);
wchar_t *memory_file(const wchar_t *name);
wchar_t *local_file(const wchar_t *name);

cJSON *json_load_object(const wchar_t *path);
bool json_save(const wchar_t *path, const cJSON *obj);

const char *current_speaker(void);
void set_current_speaker(const char *key);
void memory_identify_on_start(const char *user_name);

void memory_persist(const char *role, const char *content);
cJSON *memory_recent_history(double window_seconds);

char *hash_password(const char *password);

typedef struct {
    char *profile;
    char *texto;
} DueReminder;

int reminders_take_due(DueReminder **out);
char *reminders_take_pending_for(const char *profile);
void due_reminders_free(DueReminder *r, int n);
char *profile_display_name(const char *key);
int reset_all_profile_passwords(void);
bool set_profile_password(const char *name, const char *password);

bool load_calibration(int *threshold, int *runs);
void save_calibration(int threshold, int runs);

#endif
