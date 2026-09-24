#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "resource.h"
#include "resources.h"
#include "util.h"
#include "wakeword.h"

static float *read_vec(FILE *f, int *n)
{
    uint32_t count;
    if (fread(&count, 4, 1, f) != 1) return NULL;
    float *v = malloc(sizeof(float) * count);
    fread(v, sizeof(float), count, f);
    *n = (int)count;
    return v;
}

static double max_rel(const float *a, const float *b, int n)
{
    double m = 0;
    for (int i = 0; i < n; i++) {
        double d = fabs((double)a[i] - b[i]) / (fabs((double)b[i]) + 1e-3);
        if (d > m) m = d;
    }
    return m;
}

static int16_t *load_wav16k(const wchar_t *path, size_t *samples)
{
    size_t len;
    char *data = read_file_all(path, &len);
    if (!data || len < 44) return NULL;
    size_t off = 12;
    while (off + 8 <= len) {
        uint32_t sz;
        memcpy(&sz, data + off + 4, 4);
        if (!memcmp(data + off, "data", 4)) {
            if (off + 8 + sz > len) sz = (uint32_t)(len - off - 8);
            int16_t *pcm = malloc(sz);
            memcpy(pcm, data + off + 8, sz);
            *samples = sz / 2;
            free(data);
            return pcm;
        }
        off += 8 + sz + (sz & 1);
    }
    free(data);
    return NULL;
}

int wmain(int argc, wchar_t **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
    log_to_console(1);
    size_t blen;
    const void *blob = res_data(IDR_WAKEWORD, &blen);
    uint64_t t0 = now_ms();
    WakeWord *w = ww_create(blob, blen);
    printf("init: %llu ms\n", (unsigned long long)(now_ms() - t0));
    if (!w) return 1;

    if (argc > 1) {
        FILE *f = _wfopen(argv[1], L"rb");
        int n;
        float *mel_in = read_vec(f, &n), *mel_out = read_vec(f, &n);
        float *emb_in = read_vec(f, &n), *emb_out = read_vec(f, &n);
        float *kw_in = read_vec(f, &n), *kw_out = read_vec(f, &n);
        fclose(f);
        float mel[16 * 32], emb[96], p1;
        int frames = ww_melspec(w, mel_in, 1760, mel);
        printf("mel: %d frames, err relativo max %.2e\n", frames, max_rel(mel, mel_out, frames * 32));
        ww_embed(w, emb_in, emb);
        printf("emb: err relativo max %.2e\n", max_rel(emb, emb_out, 96));
        float p = ww_classify(w, kw_in, &p1);
        printf("kw: C=%.6f ref=%.6f (p1=%.4f)\n", p, kw_out[0], p1);
    }

    int16_t chunk[WW_CHUNK];
    uint32_t s = 7;
    t0 = now_ms();
    for (int i = 0; i < 100; i++) {
        for (int k = 0; k < WW_CHUNK; k++) {
            s = s * 1664525u + 1013904223u;
            chunk[k] = (int16_t)((int)(s >> 16) % 600 - 300);
        }
        ww_process(w, chunk);
    }
    printf("streaming: %.2f ms por paso de 80 ms\n", (double)(now_ms() - t0) / 100.0);

    if (argc > 2) {
        size_t n;
        int16_t *pcm = load_wav16k(argv[2], &n);
        if (!pcm) {
            printf("no pude leer el wav\n");
            return 1;
        }
        ww_reset(w);
        float best = 0;
        for (size_t off = 0; off + WW_CHUNK <= n; off += WW_CHUNK) {
            float sc = ww_process(w, pcm + off);
            if (sc > 0.05f) printf("  t=%.2fs score=%.4f\n", off / 16000.0, sc);
            if (sc > best) best = sc;
        }
        printf("wav: mejor score %.4f\n", best);
    }
    ww_destroy(w);
    return 0;
}
