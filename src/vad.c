#include <stdlib.h>
#include <string.h>

#include "vad.h"
#include "util.h"
#include "webrtc/common_audio/vad/include/webrtc_vad.h"

#define SUB 320           /* WebRTC decide de a 20 ms (320 muestras a 16 kHz) */
#define FLOOR_WINDOW 32   /* el ruido de fondo es lo más bajo de los últimos 2.5 s */
#define VOICE_OVER_FLOOR 2.0f

_Static_assert(MIC_FRAME == 4 * SUB, "un cuadro del micrófono son cuatro pedazos de 20 ms");

struct Listener {
    VadInst *vad;
    float recent[FLOOR_WINDOW];
    int nrecent, pos;
    float floor;
};

Listener *listener_create(void)
{
    Listener *l = xcalloc(1, sizeof *l);
    if (WebRtcVad_Create(&l->vad) || WebRtcVad_Init(l->vad) || WebRtcVad_set_mode(l->vad, 3)) {
        if (l->vad) WebRtcVad_Free(l->vad);
        l->vad = NULL; /* sin WebRTC queda solo el volumen, como antes */
    }
    return l;
}

void listener_destroy(Listener *l)
{
    if (!l) return;
    if (l->vad) WebRtcVad_Free(l->vad);
    free(l);
}

bool listener_feed(Listener *l, const int16_t frame[MIC_FRAME], float min_energy)
{
    int votes = 4;
    if (l->vad) {
        votes = 0;
        for (int k = 0; k < 4; k++) votes += WebRtcVad_Process(l->vad, MIC_RATE, frame + k * SUB, SUB) == 1;
    }
    /* Ruido de fondo: el mínimo de los últimos 2.5 s. Aunque hables mucho
       rato, entre sílabas y palabras baja, así que tu voz no lo arrastra; y si
       prenden un ventilador, a los 2.5 s ya lo cuenta como fondo. */
    float e = frame_energy(frame, MIC_FRAME);
    l->recent[l->pos] = e;
    l->pos = (l->pos + 1) % FLOOR_WINDOW;
    if (l->nrecent < FLOOR_WINDOW) l->nrecent++;
    float m = e;
    for (int i = 0; i < l->nrecent; i++)
        if (l->recent[i] < m) m = l->recent[i];
    l->floor = l->nrecent == 1 ? m : 0.7f * l->floor + 0.3f * m;
    float threshold = l->floor * VOICE_OVER_FLOOR;
    if (threshold < min_energy) threshold = min_energy;
    return votes >= 2 && e > threshold;
}

float listener_noise_floor(const Listener *l)
{
    return l->floor;
}

int end_silence_frames(EndSilence e)
{
    switch (e) {
    case END_SHORT: return 8; /* 0.64 s */
    case END_LONG: return 15; /* 1.2 s */
    default: return 10;       /* 0.8 s */
    }
}

/* -------------------------------------------------------------- orden --- */

static void rec_push(Recording *r, const int16_t *frame, bool voice)
{
    if (r->frames == r->cap) {
        r->cap = r->cap ? r->cap * 2 : 64;
        r->pcm = xrealloc(r->pcm, sizeof(int16_t) * MIC_FRAME * (size_t)r->cap);
        r->voiced = xrealloc(r->voiced, (size_t)r->cap);
    }
    memcpy(r->pcm + (size_t)r->frames * MIC_FRAME, frame, sizeof(int16_t) * MIC_FRAME);
    r->voiced[r->frames++] = voice;
}

void rec_begin(Recording *r, int end_frames)
{
    memset(r, 0, sizeof *r);
    r->end_frames = end_frames > 0 ? end_frames : 10;
}

void rec_seed(Recording *r, const int16_t *frames, int n)
{
    for (int i = 0; i < n; i++) rec_push(r, frames + (size_t)i * MIC_FRAME, true);
    if (n) r->heard = true;
}

bool rec_feed(Recording *r, const int16_t frame[MIC_FRAME], bool voice)
{
    if (r->done) return true;
    if (!r->heard) {
        /* Todavía no empiezas: se guardan los últimos 320 ms (por si la voz
           empieza bajito) y hacen falta dos cuadros de voz seguidos, para que
           un golpe o un clic no cuenten. */
        if (voice && ++r->onset >= 2) {
            for (int i = 0; i < r->npre; i++) rec_push(r, r->pre[i], false);
            r->heard = true;
            rec_push(r, frame, true);
            /* el cuadro anterior también era voz */
            if (r->frames >= 2) r->voiced[r->frames - 2] = true;
            return false;
        }
        if (!voice) r->onset = 0;
        if (r->npre == RECORD_PREROLL_FRAMES) {
            memmove(r->pre[0], r->pre[1], sizeof r->pre[0] * (RECORD_PREROLL_FRAMES - 1));
            r->npre--;
        }
        memcpy(r->pre[r->npre++], frame, sizeof r->pre[0]);
        if (++r->waited >= RECORD_WAIT_FRAMES) r->done = true;
        return r->done;
    }
    rec_push(r, frame, voice);
    r->silence_run = voice ? 0 : r->silence_run + 1;
    if (r->silence_run >= r->end_frames || r->frames >= RECORD_MAX_FRAMES) r->done = true;
    return r->done;
}

int16_t *rec_take(Recording *r, size_t *samples, float *voice_s)
{
    *samples = 0;
    if (voice_s) *voice_s = 0;
    int first = -1, last = -1, nvoice = 0;
    for (int i = 0; i < r->frames; i++) {
        if (!r->voiced[i]) continue;
        if (first < 0) first = i;
        last = i;
        nvoice++;
    }
    if (voice_s) *voice_s = (float)nvoice * MIC_FRAME / MIC_RATE;
    if (first < 0 || nvoice < 3) return NULL; /* menos de 240 ms de voz: nada que transcribir */
    first -= RECORD_PREROLL_FRAMES;
    if (first < 0) first = 0;
    last += 3; /* el final de la última palabra suele ser bajito */
    if (last >= r->frames) last = r->frames - 1;
    int16_t *out = xmalloc(sizeof(int16_t) * MIC_FRAME * (size_t)(last - first + 1));
    size_t n = 0;
    int gap = 0;
    for (int i = first; i <= last; i++) {
        gap = r->voiced[i] ? 0 : gap + 1;
        if (gap > 4) continue; /* una pausa larga en medio se deja en 0.3 s */
        memcpy(out + n, r->pcm + (size_t)i * MIC_FRAME, sizeof(int16_t) * MIC_FRAME);
        n += MIC_FRAME;
    }
    *samples = n;
    return out;
}

void rec_free(Recording *r)
{
    free(r->pcm);
    free(r->voiced);
    memset(r, 0, sizeof *r);
}
