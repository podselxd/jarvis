#ifndef SOKARI_RESOURCES_H
#define SOKARI_RESOURCES_H

#include <stdbool.h>
#include <stddef.h>

const void *res_data(int id, size_t *len);
char *res_string(int id);
/* ¿El exe trae el modelo de "Hey Sokari"? Sin él solo se le habla con el atajo. */
bool res_has_wake_word(void);

#endif
