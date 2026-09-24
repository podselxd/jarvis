#ifndef SOKARI_LOG_H
#define SOKARI_LOG_H

#include <wchar.h>

void log_init(const wchar_t *path);
void log_msg(const char *fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
void log_to_console(int enabled);

#endif
