/* sounds.h en Linux. Tus sonidos (activacion.mp3/.wav/.ogg y busqueda.* en
   ~/.config/sokari/sounds) suenan con paplay; sin ellos, el tono de Sokari
   (sounds_common.c). */
#include <windows.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "config.h"
#include "linux/proc.h"
#include "log.h"
#include "sounds.h"
#include "util.h"

/* Sokari empieza a escucharte a los 0.9 s aunque tu sonido siga sonando; un
   sonido de más de 10 s se corta ahí. */
#define ACTIVATION_LISTEN_MS 900
#define ACTIVATION_MAX_MS 10000

static char *user_sound(const char *base)
{
    static const char *const EXTS[] = {".mp3", ".wav", ".ogg", ".flac"};
    char *dir = wide_to_utf8(g_paths.sounds_dir);
    char *found = NULL;
    for (size_t i = 0; i < sizeof EXTS / sizeof *EXTS && !found; i++) {
        char *p = str_printf("%s/%s%s", dir, base, EXTS[i]);
        wchar_t *w = utf8_to_wide(p);
        if (file_exists(w)) found = p;
        else free(p);
        free(w);
    }
    free(dir);
    return found;
}

static pid_t play_file(const char *path)
{
    char vol[32];
    snprintf(vol, sizeof vol, "--volume=%d", config_volume() * 65536 / 100);
    const char *argv[] = {"paplay", vol, "--client-name=Sokari", path, NULL};
    return proc_spawn(argv, NULL, NULL, NULL, NULL);
}

static void stop_pid(volatile LONG *slot)
{
    pid_t pid = (pid_t)InterlockedExchange(slot, 0);
    if (pid > 0) {
        kill(pid, SIGTERM);
        proc_finish(pid, 500);
    }
}

static volatile LONG g_activation_pid;

void sound_activation(void)
{
    char *p = user_sound("activacion");
    pid_t pid = p ? play_file(p) : -1;
    free(p);
    if (pid > 0) {
        InterlockedExchange(&g_activation_pid, (LONG)pid);
        Sleep(ACTIVATION_LISTEN_MS);
    } else {
        sound_chime();
    }
}

void sound_activation_stop(void)
{
    stop_pid(&g_activation_pid);
}

/* El sonido de búsqueda se repite hasta que termina de buscar. */
static volatile LONG g_search_on, g_search_pid;
static HANDLE g_search_thread;

static DWORD WINAPI search_loop(LPVOID arg)
{
    char *p = arg;
    while (InterlockedCompareExchange(&g_search_on, 1, 1)) {
        pid_t pid = play_file(p);
        if (pid <= 0) break;
        InterlockedExchange(&g_search_pid, (LONG)pid);
        int code = proc_finish(pid, 60000);
        InterlockedExchange(&g_search_pid, 0);
        if (code != 0) break; /* no se pudo tocar, o lo cortaron */
    }
    free(p);
    return 0;
}

void sound_search_start(void)
{
    char *p = user_sound("busqueda");
    if (!p) return;
    sound_search_stop();
    InterlockedExchange(&g_search_on, 1);
    g_search_thread = CreateThread(NULL, 0, search_loop, p, 0, NULL);
    if (!g_search_thread) free(p);
}

void sound_search_stop(void)
{
    InterlockedExchange(&g_search_on, 0);
    stop_pid(&g_search_pid);
    if (g_search_thread) {
        WaitForSingleObject(g_search_thread, 1000);
        CloseHandle(g_search_thread);
        g_search_thread = NULL;
    }
}
