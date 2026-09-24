#ifndef JARVIS_AUDIO_H
#define JARVIS_AUDIO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MIC_RATE 16000
#define MIC_FRAME 1280

/* Micrófono: un hilo propio drena waveIn a un buffer circular; el hilo de voz
   lee de a 80 ms. Así, mientras Jarvis piensa o habla, no se pierde audio. */
bool mic_start(const char *device_name);
void mic_stop(void);
bool mic_restart(const char *device_name);
bool mic_read(int16_t out[MIC_FRAME], unsigned timeout_ms);
bool mic_read_nowait(int16_t out[MIC_FRAME]);
void mic_flush(void);
float mic_level(void);
int mic_list_devices(char ***names_out);
void free_string_list(char **list, int n);

/* Reproducción PCM mono 16 bits. El callback se llama cada ~40 ms con el
   nivel (0..1) de lo que está sonando; si devuelve false se corta ahí. */
typedef bool (*PlayCallback)(float level, void *ctx);
bool speaker_play(const int16_t *pcm, size_t samples, int rate, float gain, PlayCallback cb, void *ctx);

/* Salida de audio por nombre (el que da Windows, como en mic_list_devices);
   NULL o "" = la predeterminada. Si esa salida no está conectada, se usa la
   predeterminada. */
void speaker_set_device(const char *name);
int speaker_list_devices(char ***names_out);

float frame_energy(const int16_t *pcm, size_t n);
float volume_to_gain(int volume);

#endif
