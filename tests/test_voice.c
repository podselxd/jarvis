#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "groq.h"
#include "http.h"
#include "log.h"
#include "tts.h"
#include "util.h"

static int16_t *load_wav(const wchar_t *path, size_t *samples)
{
    size_t len;
    char *d = read_file_all(path, &len);
    if (!d) return NULL;
    size_t off = 12;
    while (off + 8 <= len) {
        uint32_t sz;
        memcpy(&sz, d + off + 4, 4);
        if (!memcmp(d + off, "data", 4)) {
            int16_t *pcm = malloc(sz);
            memcpy(pcm, d + off + 8, sz);
            *samples = sz / 2;
            return pcm;
        }
        off += 8 + sz + (sz & 1);
    }
    return NULL;
}

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
    log_to_console(1);
    paths_init();
    config_load();
    http_init();

    uint64_t t0 = now_ms();
    tts_init(NULL);
    printf("tts_init: %llu ms\n", (unsigned long long)(now_ms() - t0));
    TtsVoice *v;
    int n = tts_list_voices(&v);
    for (int i = 0; i < n; i++) printf("  voz: %s\n", v[i].name);
    tts_free_voices(v, n);

    const char *text = "Hola, soy Sokari. Ya estoy listo para ayudarte en lo que necesites.";
    size_t samples;
    t0 = now_ms();
    int16_t *pcm = tts_synthesize(text, &samples);
    printf("sintesis: %llu ms -> %.2f s de audio\n", (unsigned long long)(now_ms() - t0), samples / (double)TTS_RATE);
    if (argc > 2 && pcm) {
        size_t wl;
        unsigned char *wav = wav_encode(pcm, samples, TTS_RATE, &wl);
#ifdef _WIN32
        write_file_atomic(argv[2], wav, wl);
#else
        wchar_t *out = utf8_to_wide(argv[2]);
        write_file_atomic(out, wav, wl);
        free(out);
#endif
        printf("wav escrito\n");
    }
    if (argc > 1) {
        size_t s16;
#ifdef _WIN32
        int16_t *in = load_wav(argv[1], &s16);
#else
        wchar_t *inw = utf8_to_wide(argv[1]);
        int16_t *in = load_wav(inw, &s16);
        free(inw);
#endif
        GroqError err = {0};
        t0 = now_ms();
        char *txt = in ? groq_transcribe(in, s16, 16000, &err) : NULL;
        printf("stt: %llu ms -> [%s] (err=%d %s)\n", (unsigned long long)(now_ms() - t0), txt ? txt : "(null)",
               err.status, err.detail ? err.detail : "");
    }
    tts_shutdown();
    return 0;
}
