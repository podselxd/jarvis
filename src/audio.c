#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "log.h"
#include "util.h"

#define MIC_BUFFERS 16
#define RING_SAMPLES (MIC_RATE * 30)

static HWAVEIN g_hwi;
static WAVEHDR g_hdr[MIC_BUFFERS];
static int16_t *g_buf[MIC_BUFFERS];
static HANDLE g_event;
static HANDLE g_thread;
static volatile LONG g_running;

static int16_t *g_ring;
static size_t g_ring_start, g_ring_count;
static SRWLOCK g_ring_lock = SRWLOCK_INIT;
static CONDITION_VARIABLE g_ring_cv = CONDITION_VARIABLE_INIT;
static volatile LONG g_level_milli;
static volatile LONG64 g_last_data_ms;

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

static void ring_push(const int16_t *s, size_t n)
{
    AcquireSRWLockExclusive(&g_ring_lock);
    for (size_t i = 0; i < n; i++) {
        size_t pos = (g_ring_start + g_ring_count) % RING_SAMPLES;
        g_ring[pos] = s[i];
        if (g_ring_count < RING_SAMPLES)
            g_ring_count++;
        else
            g_ring_start = (g_ring_start + 1) % RING_SAMPLES;
    }
    ReleaseSRWLockExclusive(&g_ring_lock);
    WakeAllConditionVariable(&g_ring_cv);
}

static bool ring_pop(int16_t *out, size_t n)
{
    if (g_ring_count < n) return false;
    for (size_t i = 0; i < n; i++) out[i] = g_ring[(g_ring_start + i) % RING_SAMPLES];
    g_ring_start = (g_ring_start + n) % RING_SAMPLES;
    g_ring_count -= n;
    return true;
}

static DWORD WINAPI capture_thread(LPVOID arg)
{
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        WaitForSingleObject(g_event, 200);
        for (int i = 0; i < MIC_BUFFERS; i++) {
            WAVEHDR *h = &g_hdr[i];
            if (!(h->dwFlags & WHDR_DONE)) continue;
            if (h->dwBytesRecorded) {
                size_t n = h->dwBytesRecorded / 2;
                ring_push((int16_t *)h->lpData, n);
                float e = frame_energy((int16_t *)h->lpData, n);
                float lvl = e / 3000.0f;
                if (lvl > 1) lvl = 1;
                InterlockedExchange(&g_level_milli, (LONG)(lvl * 1000));
                InterlockedExchange64(&g_last_data_ms, (LONG64)GetTickCount64());
            }
            h->dwFlags &= ~WHDR_DONE;
            h->dwBytesRecorded = 0;
            if (InterlockedCompareExchange(&g_running, 1, 1)) waveInAddBuffer(g_hwi, h, sizeof *h);
        }
    }
    return 0;
}

static UINT find_device(const char *name)
{
    if (!name || !*name) return WAVE_MAPPER;
    UINT n = waveInGetNumDevs();
    for (UINT i = 0; i < n; i++) {
        WAVEINCAPSW caps;
        if (waveInGetDevCapsW(i, &caps, sizeof caps) != MMSYSERR_NOERROR) continue;
        char *pname = wide_to_utf8(caps.szPname);
        bool match = strcmp(pname, name) == 0;
        free(pname);
        if (match) return i;
    }
    return WAVE_MAPPER;
}

bool mic_start(const char *device_name)
{
    if (!g_ring) g_ring = xmalloc(sizeof(int16_t) * RING_SAMPLES);
    g_ring_start = g_ring_count = 0;
    WAVEFORMATEX fmt = {.wFormatTag = WAVE_FORMAT_PCM, .nChannels = 1, .nSamplesPerSec = MIC_RATE,
                        .wBitsPerSample = 16, .nBlockAlign = 2, .nAvgBytesPerSec = MIC_RATE * 2};
    g_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    UINT dev = find_device(device_name);
    MMRESULT r = waveInOpen(&g_hwi, dev, &fmt, (DWORD_PTR)g_event, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR && dev != WAVE_MAPPER)
        r = waveInOpen(&g_hwi, WAVE_MAPPER, &fmt, (DWORD_PTR)g_event, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        log_msg("No pude abrir el micrófono (código %u).", (unsigned)r);
        CloseHandle(g_event);
        g_event = NULL;
        g_hwi = NULL;
        return false;
    }
    for (int i = 0; i < MIC_BUFFERS; i++) {
        if (!g_buf[i]) g_buf[i] = xmalloc(sizeof(int16_t) * MIC_FRAME);
        memset(&g_hdr[i], 0, sizeof g_hdr[i]);
        g_hdr[i].lpData = (LPSTR)g_buf[i];
        g_hdr[i].dwBufferLength = sizeof(int16_t) * MIC_FRAME;
        waveInPrepareHeader(g_hwi, &g_hdr[i], sizeof g_hdr[i]);
        waveInAddBuffer(g_hwi, &g_hdr[i], sizeof g_hdr[i]);
    }
    InterlockedExchange(&g_running, 1);
    InterlockedExchange64(&g_last_data_ms, (LONG64)GetTickCount64());
    g_thread = CreateThread(NULL, 0, capture_thread, NULL, 0, NULL);
    SetThreadPriority(g_thread, THREAD_PRIORITY_TIME_CRITICAL);
    waveInStart(g_hwi);
    return true;
}

void mic_stop(void)
{
    if (!g_hwi) return;
    InterlockedExchange(&g_running, 0);
    SetEvent(g_event);
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
    g_thread = NULL;
    waveInReset(g_hwi);
    for (int i = 0; i < MIC_BUFFERS; i++) waveInUnprepareHeader(g_hwi, &g_hdr[i], sizeof g_hdr[i]);
    waveInClose(g_hwi);
    g_hwi = NULL;
    CloseHandle(g_event);
    g_event = NULL;
    WakeAllConditionVariable(&g_ring_cv);
}

bool mic_restart(const char *device_name)
{
    mic_stop();
    return mic_start(device_name);
}

bool mic_read(int16_t out[MIC_FRAME], unsigned timeout_ms)
{
    uint64_t deadline = GetTickCount64() + timeout_ms;
    AcquireSRWLockExclusive(&g_ring_lock);
    while (g_ring_count < MIC_FRAME) {
        uint64_t now = GetTickCount64();
        if (now >= deadline || !g_hwi) {
            ReleaseSRWLockExclusive(&g_ring_lock);
            return false;
        }
        SleepConditionVariableSRW(&g_ring_cv, &g_ring_lock, (DWORD)(deadline - now), 0);
    }
    bool ok = ring_pop(out, MIC_FRAME);
    ReleaseSRWLockExclusive(&g_ring_lock);
    return ok;
}

bool mic_read_nowait(int16_t out[MIC_FRAME])
{
    AcquireSRWLockExclusive(&g_ring_lock);
    bool ok = ring_pop(out, MIC_FRAME);
    ReleaseSRWLockExclusive(&g_ring_lock);
    return ok;
}

void mic_flush(void)
{
    AcquireSRWLockExclusive(&g_ring_lock);
    g_ring_start = g_ring_count = 0;
    ReleaseSRWLockExclusive(&g_ring_lock);
}

float mic_level(void)
{
    if (GetTickCount64() - (uint64_t)InterlockedCompareExchange64(&g_last_data_ms, 0, 0) > 500) return 0;
    return (float)InterlockedCompareExchange(&g_level_milli, 0, 0) / 1000.0f;
}

int mic_list_devices(char ***names_out)
{
    UINT n = waveInGetNumDevs();
    char **names = xcalloc(n + 1, sizeof(char *));
    int count = 0;
    for (UINT i = 0; i < n; i++) {
        WAVEINCAPSW caps;
        if (waveInGetDevCapsW(i, &caps, sizeof caps) == MMSYSERR_NOERROR) names[count++] = wide_to_utf8(caps.szPname);
    }
    *names_out = names;
    return count;
}

void free_string_list(char **list, int n)
{
    if (!list) return;
    for (int i = 0; i < n; i++) free(list[i]);
    free(list);
}

#define PLAY_BUFFERS 4
#define PLAY_CHUNK_MS 40

bool speaker_play(const int16_t *pcm, size_t samples, int rate, float gain, PlayCallback cb, void *ctx)
{
    WAVEFORMATEX fmt = {.wFormatTag = WAVE_FORMAT_PCM, .nChannels = 1, .nSamplesPerSec = (DWORD)rate,
                        .wBitsPerSample = 16, .nBlockAlign = 2, .nAvgBytesPerSec = (DWORD)rate * 2};
    HANDLE ev = CreateEventW(NULL, FALSE, FALSE, NULL);
    HWAVEOUT hwo;
    if (waveOutOpen(&hwo, WAVE_MAPPER, &fmt, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(ev);
        log_msg("No pude abrir la salida de audio.");
        return false;
    }
    size_t chunk = (size_t)rate * PLAY_CHUNK_MS / 1000;
    WAVEHDR hdr[PLAY_BUFFERS];
    int16_t *bufs[PLAY_BUFFERS];
    float levels[PLAY_BUFFERS];
    memset(hdr, 0, sizeof hdr);
    for (int i = 0; i < PLAY_BUFFERS; i++) bufs[i] = xmalloc(sizeof(int16_t) * chunk);

    size_t pos = 0;
    int in_flight = 0;
    bool stopped = false;
    bool free_slot[PLAY_BUFFERS];
    for (int i = 0; i < PLAY_BUFFERS; i++) free_slot[i] = true;

    while (!stopped && (pos < samples || in_flight > 0)) {
        for (int i = 0; i < PLAY_BUFFERS && pos < samples; i++) {
            if (!free_slot[i]) continue;
            size_t n = samples - pos < chunk ? samples - pos : chunk;
            double sum = 0;
            for (size_t k = 0; k < n; k++) {
                float v = (float)pcm[pos + k] * gain;
                if (v > 32767) v = 32767;
                if (v < -32768) v = -32768;
                bufs[i][k] = (int16_t)v;
                sum += fabs(v);
            }
            float lvl = (float)(sum / (double)n / 4000.0);
            levels[i] = lvl > 1 ? 1 : lvl;
            hdr[i].lpData = (LPSTR)bufs[i];
            hdr[i].dwBufferLength = (DWORD)(n * 2);
            hdr[i].dwFlags = 0;
            waveOutPrepareHeader(hwo, &hdr[i], sizeof hdr[i]);
            waveOutWrite(hwo, &hdr[i], sizeof hdr[i]);
            free_slot[i] = false;
            in_flight++;
            pos += n;
        }
        WaitForSingleObject(ev, 100);
        for (int i = 0; i < PLAY_BUFFERS; i++) {
            if (free_slot[i] || !(hdr[i].dwFlags & WHDR_DONE)) continue;
            waveOutUnprepareHeader(hwo, &hdr[i], sizeof hdr[i]);
            free_slot[i] = true;
            in_flight--;
            if (cb && !cb(levels[i], ctx)) {
                stopped = true;
                break;
            }
        }
    }
    if (stopped) waveOutReset(hwo);
    for (int i = 0; i < PLAY_BUFFERS; i++) {
        if (!free_slot[i]) {
            while (!(hdr[i].dwFlags & WHDR_DONE)) Sleep(5);
            waveOutUnprepareHeader(hwo, &hdr[i], sizeof hdr[i]);
        }
        free(bufs[i]);
    }
    waveOutClose(hwo);
    CloseHandle(ev);
    if (cb) cb(0, ctx);
    return !stopped;
}
