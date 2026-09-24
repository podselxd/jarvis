#ifndef SOKARI_GROQ_H
#define SOKARI_GROQ_H

#include <stddef.h>
#include <stdint.h>

#include "third_party/cJSON.h"

typedef enum {
    GROQ_OK = 0,
    GROQ_RATE_LIMITED,
    GROQ_AUTH_ERROR,
    GROQ_NETWORK_ERROR,
    GROQ_SERVER_ERROR,
    GROQ_BAD_RESPONSE,
} GroqStatus;

typedef struct {
    GroqStatus status;
    int http_status;
    int retry_after;
    char *detail;
} GroqError;

void groq_error_free(GroqError *e);

/* Texto transcripto (puede ser "" si no había voz real), o NULL si falló. */
char *groq_transcribe(const int16_t *pcm, size_t samples, int sample_rate, GroqError *err);

/* Devuelve el "message" de choices[0] ya limpio (role/content/tool_calls),
   o NULL si falló. Prueba con un modelo de respaldo si el principal está
   sin cupo (cada modelo de Groq tiene su propio límite). */
cJSON *groq_chat(const cJSON *messages, const cJSON *tools, GroqError *err);

unsigned char *wav_encode(const int16_t *pcm, size_t samples, int sample_rate, size_t *out_len);

#endif
