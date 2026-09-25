#ifndef SOKARI_VOICE_H
#define SOKARI_VOICE_H

#include <stdbool.h>
#include <stdint.h>

#include "tts.h"

bool voice_start(void);
void voice_stop(void);
void voice_trigger(void);
void voice_settings_changed(void);
bool voice_wait(unsigned ms);
bool voice_running(void);
/* Tono + una frase por la salida de audio elegida (cuando Sokari no está en
   medio de una conversación). */
void voice_test_audio(void);
/* Dice una frase con esa voz (sin guardarla), para elegir desde
   Configuración. Como voice_test_audio, espera a que no haya conversación. */
void voice_preview(const char *voice_id);

/* Voces disponibles, pedidas al hilo de síntesis (SAPI vive en ese hilo). */
int voice_list_voices(TtsVoice **out);

/* Modo simulación para pruebas: en vez del micrófono, lee un .wav de 16 kHz. */
void voice_set_input_wav(const wchar_t *path);

#endif
