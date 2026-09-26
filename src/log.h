#ifndef SOKARI_LOG_H
#define SOKARI_LOG_H

#include <wchar.h>

void log_init(const wchar_t *path);
void log_msg(const char *fmt, ...) __attribute__((format(gnu_printf, 1, 2)));
void log_to_console(int enabled);

/* Cuántas llamadas a la IA y cuántos tokens gastó el turno de ahora (para la
   línea de «Tiempos» del log). */
typedef struct {
    int calls, tokens_in, tokens_out;
} TurnStats;
void turn_stats_reset(void);
void turn_stats_llm(int tokens_in, int tokens_out);
TurnStats turn_stats_get(void);

#endif
