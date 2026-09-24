#ifndef JARVIS_RESOURCES_H
#define JARVIS_RESOURCES_H

#include <stddef.h>

const void *res_data(int id, size_t *len);
char *res_string(int id);

#endif
