#ifndef JARVIS_WAKEWORD_H
#define JARVIS_WAKEWORD_H

#include <stddef.h>
#include <stdint.h>

#define WW_CHUNK 1280 /* 80 ms a 16 kHz: lo que consume el modelo por paso */

typedef struct WakeWord WakeWord;

WakeWord *ww_create(const void *blob, size_t len);
void ww_destroy(WakeWord *w);
void ww_reset(WakeWord *w);
float ww_process(WakeWord *w, const int16_t *chunk);

/* Piezas sueltas, expuestas para poder verificarlas contra onnxruntime. */
int ww_melspec(WakeWord *w, const float *samples, int n, float *out);
void ww_embed(WakeWord *w, const float *mel76x32, float *out96);
float ww_classify(WakeWord *w, const float *feat16x96, float *p1_out);

#endif
