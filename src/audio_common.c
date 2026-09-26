/* Lo del audio que no depende del sistema. */
#include <stdlib.h>

#include "audio.h"

float frame_energy(const int16_t *pcm, size_t n)
{
    /* media del valor absoluto, igual que la versión anterior (sus umbrales
       de silencio calibrados siguen valiendo); en int32 para que -32768 no
       desborde. */
    if (!n) return 0;
    int64_t sum = 0;
    for (size_t i = 0; i < n; i++) sum += abs((int32_t)pcm[i]);
    return (float)((double)sum / (double)n);
}

float volume_to_gain(int volume)
{
    if (volume <= 0) return 0.0f;
    if (volume >= 100) return 1.0f;
    float v = (float)volume / 100.0f;
    return v * v;
}
