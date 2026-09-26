/* El tono de Sokari: generado aquí, igual en Windows y en Linux. */
#include <math.h>
#include <stdlib.h>

#include "audio.h"
#include "config.h"
#include "sounds.h"
#include "util.h"

void sound_chime(void)
{
    const int rate = 24000;
    const double notes[2] = {880.0, 1318.5};
    const int note_len = rate * 75 / 1000, gap = rate * 15 / 1000;
    size_t n = (size_t)(note_len * 2 + gap);
    int16_t *pcm = xcalloc(n, sizeof(int16_t));
    for (int k = 0; k < 2; k++) {
        size_t off = (size_t)k * (size_t)(note_len + gap);
        for (int i = 0; i < note_len; i++) {
            double t = (double)i / rate;
            double env = fmin(1.0, i / (rate * 0.004)) * exp(-t * 28.0);
            double s = sin(2 * M_PI * notes[k] * t) + 0.25 * sin(4 * M_PI * notes[k] * t);
            pcm[off + (size_t)i] = (int16_t)(s * env * 5200.0);
        }
    }
    speaker_play(pcm, n, rate, volume_to_gain(config_volume()), NULL, NULL);
    free(pcm);
}
