#ifndef SOKARI_VAD_H
#define SOKARI_VAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "audio.h"

/* Saber cuándo hablas y cuándo no, aunque haya ruido.

   Una sola cosa no alcanza: el detector de voz de WebRTC, solo, toma por voz
   un ventilador o un zumbido fuerte, y el volumen, solo, toma por voz
   cualquier ruido. Juntos: es voz si WebRTC dice que sí Y suena por lo menos
   el doble que el ruido de fondo de tu cuarto (que se va midiendo solo). Con
   ruido constante (ventilador, zumbido, ruido blanco) funciona aunque tu voz
   suene apenas 6 dB por encima; con música o una tele, no del todo (por eso
   Sokari baja el volumen de la PC mientras te escucha). */

typedef struct Listener Listener;

Listener *listener_create(void);
void listener_destroy(Listener *l);
/* Un cuadro de 80 ms del micrófono: aprende el ruido de fondo y dice si es
   voz. min_energy es el umbral de silencio calibrado al arrancar (nunca se
   toma por voz algo más bajito que eso). Hay que pasarle también el audio de
   cuando nadie habla, para que aprenda el ruido de tu cuarto. */
bool listener_feed(Listener *l, const int16_t frame[MIC_FRAME], float min_energy);
float listener_noise_floor(const Listener *l);

/* Cuánto esperar callado antes de dar por terminada la orden. */
typedef enum { END_SHORT, END_NORMAL, END_LONG } EndSilence;
int end_silence_frames(EndSilence e);

#define RECORD_MAX_FRAMES (30 * MIC_RATE / MIC_FRAME) /* 30 s */
#define RECORD_WAIT_FRAMES (5 * MIC_RATE / MIC_FRAME) /* 5 s para empezar a hablar */
#define RECORD_PREROLL_FRAMES 4                       /* 320 ms antes: no se come el inicio */

/* Una orden: empieza cuando oye voz (dos cuadros seguidos), termina cuando
   te callas (end_frames cuadros sin voz, aunque siga el ruido) o a los 30 s. */
typedef struct {
    int16_t *pcm;
    unsigned char *voiced; /* por cuadro */
    int frames, cap;
    int end_frames;
    int silence_run, waited, onset;
    bool heard, done;
    int16_t pre[RECORD_PREROLL_FRAMES][MIC_FRAME];
    int npre;
} Recording;

void rec_begin(Recording *r, int end_frames);
/* El audio que ya se sabe que es tu voz (lo que dijiste pegado a "Hey Sokari"). */
void rec_seed(Recording *r, const int16_t *frames, int n);
/* Devuelve true cuando la orden ya terminó. */
bool rec_feed(Recording *r, const int16_t frame[MIC_FRAME], bool voice);
/* Solo tu voz: sin el ruido del principio ni del final, y las pausas largas
   de en medio acortadas a 0.3 s. NULL si no hubo voz (entonces no se manda
   nada a transcribir). *voice_s: cuántos segundos fueron voz. */
int16_t *rec_take(Recording *r, size_t *samples, float *voice_s);
void rec_free(Recording *r);

#endif
