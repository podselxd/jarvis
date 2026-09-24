/* Detector de la palabra: la parte común embebida (mel + embeddings) con un
   clasificador sintético de prueba, y con el de "Hey Sokari" si el exe lo
   trae. Revisa que sin clasificador no haya detector, que el clasificador dé
   lo mismo que la cuenta hecha a mano, que un archivo con otra forma se
   rechace, y mide cuánto tarda cada paso de 80 ms. */
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

/* Puntaje más alto de un wav de tests/datos (se busca junto al exe:
   build/tests/ -> ../../tests/datos), empezando con el detector limpio.
   Devuelve -1 si no está el archivo. */
static float best_score(WakeWord *w, const wchar_t *name)
{
    wchar_t *dir = exe_dir(), *rel = path_join(L"..\\..\\tests\\datos", name), *path = path_join(dir, rel);
    size_t n = 0;
    int16_t *pcm = load_wav16k(path, &n);
    free(dir);
    free(rel);
    free(path);
    if (!pcm) return -1;
    ww_reset(w);
    float best = 0;
    for (size_t off = 0; off + WW_CHUNK <= n; off += WW_CHUNK) {
        float sc = ww_process(w, pcm + off);
        if (sc > best) best = sc;
    }
    free(pcm);
    return best;
}

static int g_fail, g_total;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) g_fail++;
}

typedef struct {
    unsigned char *data;
    size_t len, cap;
    uint32_t count;
} Blob;

static void put(Blob *b, const void *p, size_t n)
{
    if (b->len + n > b->cap) {
        b->cap = (b->len + n) * 2;
        b->data = realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

static void put_tensor(Blob *b, const char *name, int d0, int d1, const float *v)
{
    uint32_t nl = (uint32_t)strlen(name), nd = d1 ? 2 : 1, a = (uint32_t)d0, c = (uint32_t)d1;
    put(b, &nl, 4);
    put(b, name, nl);
    put(b, &nd, 4);
    put(b, &a, 4);
    if (d1) put(b, &c, 4);
    put(b, v, sizeof(float) * (size_t)d0 * (d1 ? d1 : 1));
    b->count++;
}

static uint32_t g_seed = 12345;
static float rnd(float scale)
{
    g_seed = g_seed * 1664525u + 1013904223u;
    return ((float)(g_seed >> 8) / 16777216.0f * 2.0f - 1.0f) * scale;
}

/* Un clasificador como el que entrena openWakeWord (una capa oculta), con
   pesos al azar. Los guarda también sueltos para la cuenta a mano. */
typedef struct {
    int h;
    float *fc1_w, *fc1_b, *ln1_w, *ln1_b, *fc2_w, *fc2_b, *ln2_w, *ln2_b, *fc3_w, *fc3_b;
} Clf;

static float *vec(int n, float scale, float base)
{
    float *v = malloc(sizeof(float) * (size_t)n);
    for (int i = 0; i < n; i++) v[i] = base + rnd(scale);
    return v;
}

static Clf make_clf(int h)
{
    Clf c = {h};
    c.fc1_w = vec(h * 1536, 0.03f, 0);
    c.fc1_b = vec(h, 0.1f, 0);
    c.ln1_w = vec(h, 0.2f, 1);
    c.ln1_b = vec(h, 0.1f, 0);
    c.fc2_w = vec(h * h, 0.2f, 0);
    c.fc2_b = vec(h, 0.1f, 0);
    c.ln2_w = vec(h, 0.2f, 1);
    c.ln2_b = vec(h, 0.1f, 0);
    c.fc3_w = vec(h, 0.3f, 0);
    c.fc3_b = vec(1, 0.1f, 0);
    return c;
}

static Blob clf_blob(const Clf *c, bool wrong_shape)
{
    Blob b = {0};
    put(&b, "JWW1", 4);
    put(&b, &b.count, 4); /* se corrige al final */
    int h = c->h;
    put_tensor(&b, "kw.fc1.w", h, 1536, c->fc1_w);
    put_tensor(&b, "kw.fc1.b", h, 0, c->fc1_b);
    put_tensor(&b, "kw.ln1.w", h, 0, c->ln1_w);
    put_tensor(&b, "kw.ln1.b", h, 0, c->ln1_b);
    put_tensor(&b, "kw.fc2.w", h, wrong_shape ? h - 1 : h, c->fc2_w);
    put_tensor(&b, "kw.fc2.b", h, 0, c->fc2_b);
    put_tensor(&b, "kw.ln2.w", h, 0, c->ln2_w);
    put_tensor(&b, "kw.ln2.b", h, 0, c->ln2_b);
    put_tensor(&b, "kw.fc3.w", 1, h, c->fc3_w);
    put_tensor(&b, "kw.fc3.b", 1, 0, c->fc3_b);
    memcpy(b.data + 4, &b.count, 4);
    return b;
}

static void ref_layer(const float *x, int n_in, const float *w, const float *bias, int n_out, float *y)
{
    for (int o = 0; o < n_out; o++) {
        double s = bias[o];
        for (int i = 0; i < n_in; i++) s += (double)w[(size_t)o * n_in + i] * x[i];
        y[o] = (float)s;
    }
}

static void ref_ln_relu(float *x, int n, const float *g, const float *b)
{
    double mean = 0, var = 0;
    for (int i = 0; i < n; i++) mean += x[i];
    mean /= n;
    for (int i = 0; i < n; i++) var += (x[i] - mean) * (x[i] - mean);
    var /= n;
    for (int i = 0; i < n; i++) {
        double v = (x[i] - mean) / sqrt(var + 1e-5) * g[i] + b[i];
        x[i] = (float)(v > 0 ? v : 0);
    }
}

static float ref_classify(const Clf *c, const float *feat)
{
    float h1[256], h2[256], z;
    ref_layer(feat, 1536, c->fc1_w, c->fc1_b, c->h, h1);
    ref_ln_relu(h1, c->h, c->ln1_w, c->ln1_b);
    ref_layer(h1, c->h, c->fc2_w, c->fc2_b, c->h, h2);
    ref_ln_relu(h2, c->h, c->ln2_w, c->ln2_b);
    ref_layer(h2, c->h, c->fc3_w, c->fc3_b, 1, &z);
    return 1.0f / (1.0f + expf(-z));
}

int wmain(int argc, wchar_t **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
    log_to_console(1);
    size_t blen;
    const void *blob = res_data(IDR_WAKEWORD, &blen);

    check(ww_create(blob, blen, NULL, 0) == NULL, "sin clasificador de la palabra no hay detector");
    Clf small = make_clf(32), big = make_clf(128);
    Blob bs = clf_blob(&small, false), bb = clf_blob(&big, false), bad = clf_blob(&small, true);
    check(ww_create(blob, blen, bad.data, bad.len) == NULL, "un clasificador con otra forma se rechaza");

    uint64_t t0 = now_ms();
    WakeWord *w = ww_create(blob, blen, bs.data, bs.len);
    printf("init: %llu ms\n", (unsigned long long)(now_ms() - t0));
    check(w != NULL, "carga la parte común con un clasificador de 32 neuronas");
    WakeWord *w2 = ww_create(blob, blen, bb.data, bb.len);
    check(w2 != NULL, "y con uno de 128");
    if (!w || !w2) {
        printf("%d/%d pruebas — HAY FALLAS\n", g_total - g_fail, g_total);
        return 1;
    }
    double worst = 0;
    for (int k = 0; k < 20; k++) {
        float feat[1536];
        for (int i = 0; i < 1536; i++) feat[i] = rnd(3.0f);
        double d1 = fabs(ww_classify(w, feat) - ref_classify(&small, feat));
        double d2 = fabs(ww_classify(w2, feat) - ref_classify(&big, feat));
        if (d1 > worst) worst = d1;
        if (d2 > worst) worst = d2;
    }
    printf("      diferencia máxima con la cuenta a mano: %.2e\n", worst);
    check(worst < 1e-4, "el clasificador da lo mismo que la cuenta hecha a mano");

    if (argc > 1) {
        FILE *f = _wfopen(argv[1], L"rb");
        int n;
        float *mel_in = read_vec(f, &n), *mel_out = read_vec(f, &n);
        float *emb_in = read_vec(f, &n), *emb_out = read_vec(f, &n);
        fclose(f);
        float mel[16 * 32], emb[96];
        int frames = ww_melspec(w, mel_in, 1760, mel);
        printf("mel: %d frames, err relativo max %.2e\n", frames, max_rel(mel, mel_out, frames * 32));
        ww_embed(w, emb_in, emb);
        printf("emb: err relativo max %.2e\n", max_rel(emb, emb_out, 96));
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
    ww_destroy(w);
    ww_destroy(w2);

    size_t wlen = 0;
    const void *word = res_data(IDR_HEY_SOKARI, &wlen);
    if (!word) {
        printf("(este exe no trae el modelo de \"Hey Sokari\": se omiten sus pruebas)\n");
    } else {
        WakeWord *hs = ww_create(blob, blen, word, wlen);
        check(hs != NULL, "el modelo de \"Hey Sokari\" que trae el exe carga");
        if (hs) {
            /* Clips con margen, para notar si algo se rompe (el modelo, su
               conversión o el detector). La precisión real está medida en
               herramientas/LEEME.md. Umbral: el de fábrica (sensibilidad 67). */
            const float umbral = 0.4f;
            float si = best_score(hs, L"hey_sokari.wav");
            float saf = best_score(hs, L"hey_safari.wav"), soc = best_score(hs, L"oye_socorro.wav");
            printf("      \"hey sokari\" a la española: %.3f | \"hey safari\": %.3f | \"oye socorro\": %.3f\n", si, saf,
                   soc);
            check(si > umbral, "detecta \"hey sokari\" (voz sintética nunca vista)");
            check(saf >= 0 && saf < umbral, "no se activa con \"hey safari\"");
            check(soc >= 0 && soc < umbral, "no se activa con \"oye socorro\"");
            ww_reset(hs);
            float ruido = 0;
            uint32_t r = 99;
            for (int i = 0; i < 250; i++) { /* 20 s de ruido */
                for (int k = 0; k < WW_CHUNK; k++) {
                    r = r * 1664525u + 1013904223u;
                    chunk[k] = (int16_t)((int)(r >> 16) % 4000 - 2000);
                }
                float sc = ww_process(hs, chunk);
                if (sc > ruido) ruido = sc;
            }
            printf("      20 s de ruido: %.3f\n", ruido);
            check(ruido < umbral, "no se activa con ruido");
        }
        if (hs && argc > 2) {
            size_t n;
            int16_t *pcm = load_wav16k(argv[2], &n);
            float best = 0;
            for (size_t off = 0; pcm && off + WW_CHUNK <= n; off += WW_CHUNK) {
                float sc = ww_process(hs, pcm + off);
                if (sc > 0.05f) printf("  t=%.2fs score=%.4f\n", off / 16000.0, sc);
                if (sc > best) best = sc;
            }
            printf("wav: mejor score %.4f\n", best);
            free(pcm);
        }
        ww_destroy(hs);
    }
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
