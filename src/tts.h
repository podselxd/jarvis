#ifndef JARVIS_TTS_H
#define JARVIS_TTS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TTS_RATE 24000

typedef struct {
    char *id;   /* id del token SAPI (registro) */
    char *name; /* nombre para mostrar */
} TtsVoice;

/* Todas las funciones de TTS deben llamarse desde el mismo hilo (COM). */
bool tts_init(const char *preferred_voice);
void tts_shutdown(void);
bool tts_set_voice(const char *id);
int tts_list_voices(TtsVoice **out);
void tts_free_voices(TtsVoice *v, int n);
int16_t *tts_synthesize(const char *text, size_t *samples);
char *tts_clean_text(const char *text);

#endif
