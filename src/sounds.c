#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "audio.h"
#include "config.h"
#include "log.h"
#include "sounds.h"
#include "util.h"

#define ACTIVATION_MAX_MS 900

static bool mci(const wchar_t *cmd)
{
    return mciSendStringW(cmd, NULL, 0, NULL) == 0;
}

static wchar_t *user_sound(const wchar_t *base)
{
    const wchar_t *exts[] = {L".mp3", L".wav"};
    for (int i = 0; i < 2; i++) {
        wchar_t name[64];
        swprintf(name, 64, L"%ls%ls", base, exts[i]);
        wchar_t *p = path_join(g_paths.sounds_dir, name);
        if (file_exists(p)) return p;
        free(p);
    }
    return NULL;
}

static bool mci_open(const wchar_t *path, const wchar_t *alias)
{
    wchar_t cmd[MAX_PATH * 2 + 64];
    swprintf(cmd, sizeof cmd / sizeof *cmd, L"close %ls", alias);
    mci(cmd);
    swprintf(cmd, sizeof cmd / sizeof *cmd, L"open \"%ls\" alias %ls", path, alias);
    if (!mci(cmd)) return false;
    swprintf(cmd, sizeof cmd / sizeof *cmd, L"setaudio %ls volume to %d", alias, config_volume() * 10);
    mci(cmd);
    return true;
}

static void builtin_chime(void)
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

void sound_activation(void)
{
    wchar_t *p = user_sound(L"activacion");
    if (p && mci_open(p, L"jarvis_activacion")) {
        mci(L"play jarvis_activacion");
        Sleep(ACTIVATION_MAX_MS);
        mci(L"stop jarvis_activacion");
        mci(L"close jarvis_activacion");
    } else {
        builtin_chime();
    }
    free(p);
}

void sound_search_start(void)
{
    wchar_t *p = user_sound(L"busqueda");
    if (p && mci_open(p, L"jarvis_busqueda")) {
        wchar_t cmd[96];
        swprintf(cmd, 96, L"play jarvis_busqueda from %d repeat", (int)(GetTickCount64() % 50) * 1000);
        if (!mci(cmd)) mci(L"play jarvis_busqueda repeat");
    }
    free(p);
}

void sound_search_stop(void)
{
    mci(L"stop jarvis_busqueda");
    mci(L"close jarvis_busqueda");
}
