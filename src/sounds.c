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

/* Sokari empieza a escucharte a los 0.9 s aunque tu sonido siga sonando; un
   sonido de más de 10 s se corta ahí. */
#define ACTIVATION_LISTEN_MS 900
#define ACTIVATION_MAX_MS 10000

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

static unsigned mci_length_ms(const wchar_t *alias)
{
    wchar_t cmd[96], out[32] = L"";
    swprintf(cmd, 96, L"set %ls time format milliseconds", alias);
    mci(cmd);
    swprintf(cmd, 96, L"status %ls length", alias);
    if (mciSendStringW(cmd, out, 32, NULL) != 0) return 0;
    return (unsigned)wcstoul(out, NULL, 10);
}

/* Tu sonido suena completo mientras Sokari ya te escucha. Con audífonos no
   pasa nada; con bocinas el micrófono lo puede oír. */
void sound_activation(void)
{
    wchar_t *p = user_sound(L"activacion");
    if (p && mci_open(p, L"sokari_activacion")) {
        wchar_t cmd[96];
        if (mci_length_ms(L"sokari_activacion") > ACTIVATION_MAX_MS)
            swprintf(cmd, 96, L"play sokari_activacion from 0 to %d", ACTIVATION_MAX_MS);
        else
            wcscpy(cmd, L"play sokari_activacion");
        mci(cmd);
        Sleep(ACTIVATION_LISTEN_MS);
    } else {
        sound_chime();
    }
    free(p);
}

void sound_activation_stop(void)
{
    mci(L"stop sokari_activacion");
    mci(L"close sokari_activacion");
}

void sound_search_start(void)
{
    wchar_t *p = user_sound(L"busqueda");
    if (p && mci_open(p, L"sokari_busqueda")) {
        wchar_t cmd[96];
        swprintf(cmd, 96, L"play sokari_busqueda from %d repeat", (int)(GetTickCount64() % 50) * 1000);
        if (!mci(cmd)) mci(L"play sokari_busqueda repeat");
    }
    free(p);
}

void sound_search_stop(void)
{
    mci(L"stop sokari_busqueda");
    mci(L"close sokari_busqueda");
}
