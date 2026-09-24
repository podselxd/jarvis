/* Detector de "Hey Sokari": puerto exacto a C de los modelos ONNX de
   openWakeWord (melspectrograma -> CNN de embeddings -> clasificador) y de su
   lógica de streaming, así el .exe no necesita onnxruntime ni Python. Los
   pesos vienen embebidos en el ejecutable: la parte común (mel y embeddings,
   igual para cualquier palabra) y aparte el clasificador de la palabra, que
   es lo único que se entrena para "Hey Sokari". */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "nn.h"
#include "util.h"
#include "wakeword.h"

#define MEL_WIN 512
#define MEL_HOP 160
#define MEL_BINS 257
#define MEL_BINS_PAD 264
#define MEL_N 32
#define EMB_FRAMES 76
#define EMB_DIM 96
#define FEAT_FRAMES 16
#define FEAT_MAX 120
#define MEL_MAX 970
#define RAW_KEEP (WW_CHUNK + 3 * MEL_HOP)
#define CONV_LAYERS 20
#define ACT_MAX (EMB_FRAMES * MEL_N * EMB_DIM)

typedef struct {
    int cout, cin, kh, kw, padw;
    float *wt;
    float *b;
} ConvLayer;

typedef struct {
    float *fc1_wt, *fc1_b, *ln1_w, *ln1_b, *fc2_wt, *fc2_b, *ln2_w, *ln2_b, *fc3_wt, *fc3_b;
    int hidden;
} DenseNet;

typedef struct {
    char name[64];
    int ndim;
    int dims[4];
    int count;
    const unsigned char *raw;
} BlobTensor;

struct WakeWord {
    NnConvFn conv;
    NnMatTFn mat;
    float *mel_rt, *mel_it, *mel_w;
    ConvLayer layers[CONV_LAYERS];
    DenseNet net;
    float *act_a, *act_b;

    float raw[RAW_KEEP];
    int raw_count;
    float *melbuf;
    int mel_rows;
    float feat[FEAT_MAX][EMB_DIM];
    int feat_rows;
    float noise_feat[FEAT_FRAMES][EMB_DIM];
    int predictions;
};

static const int POOL_AFTER[CONV_LAYERS][2] = {
    [2] = {2, 2}, [6] = {1, 2}, [10] = {2, 2}, [14] = {1, 2}, [18] = {2, 2},
};

static bool parse_blob(const unsigned char *p, size_t len, BlobTensor **out, int *count)
{
    if (len < 8 || memcmp(p, "JWW1", 4) != 0) return false;
    uint32_t n;
    memcpy(&n, p + 4, 4);
    if (n > 1000) return false;
    BlobTensor *ts = xcalloc(n, sizeof *ts);
    size_t off = 8;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t name_len, ndim;
        if (off + 4 > len) goto bad;
        memcpy(&name_len, p + off, 4);
        off += 4;
        if (name_len >= sizeof ts[i].name || off + name_len + 4 > len) goto bad;
        memcpy(ts[i].name, p + off, name_len);
        off += name_len;
        memcpy(&ndim, p + off, 4);
        off += 4;
        if (ndim < 1 || ndim > 4 || off + ndim * 4 > len) goto bad;
        size_t count = 1;
        for (uint32_t d = 0; d < ndim; d++) {
            uint32_t v;
            memcpy(&v, p + off, 4);
            off += 4;
            ts[i].dims[d] = (int)v;
            count *= v;
        }
        ts[i].ndim = (int)ndim;
        ts[i].count = (int)count;
        if (count > 10000000 || off + count * 4 > len) goto bad;
        ts[i].raw = p + off;
        off += count * 4;
    }
    *out = ts;
    *count = (int)n;
    return true;
bad:
    free(ts);
    return false;
}

static const BlobTensor *find(const BlobTensor *ts, int n, const char *name)
{
    for (int i = 0; i < n; i++)
        if (!strcmp(ts[i].name, name)) return &ts[i];
    return NULL;
}

static float *copy_floats(const BlobTensor *t)
{
    float *f = xmalloc(sizeof(float) * (size_t)t->count);
    memcpy(f, t->raw, sizeof(float) * (size_t)t->count);
    return f;
}

/* Gemm con transB=1 guarda W[out][in]; el kernel quiere [in][out]. */
static float *transpose(const BlobTensor *t, int pad_cols)
{
    int rows = t->dims[0], cols = t->dims[1];
    int out_cols = pad_cols > rows ? pad_cols : rows;
    float *src = copy_floats(t);
    float *dst = xcalloc((size_t)cols * out_cols, sizeof(float));
    for (int r = 0; r < rows; r++)
        for (int c = 0; c < cols; c++) dst[(size_t)c * out_cols + r] = src[(size_t)r * cols + c];
    free(src);
    return dst;
}

static void free_model(WakeWord *w)
{
    free(w->mel_rt);
    free(w->mel_it);
    free(w->mel_w);
    for (int i = 0; i < CONV_LAYERS; i++) {
        free(w->layers[i].wt);
        free(w->layers[i].b);
    }
    DenseNet *d = &w->net;
    float *fields[] = {d->fc1_wt, d->fc1_b, d->ln1_w, d->ln1_b, d->fc2_wt,
                       d->fc2_b,  d->ln2_w, d->ln2_b, d->fc3_wt, d->fc3_b};
    for (size_t j = 0; j < sizeof fields / sizeof *fields; j++) free(fields[j]);
    free(w->act_a);
    free(w->act_b);
    free(w->melbuf);
}

void ww_destroy(WakeWord *w)
{
    if (!w) return;
    free_model(w);
    free(w);
}

/* Clasificador: fc1 -> LayerNorm -> ReLU -> fc2 -> LayerNorm -> ReLU -> fc3
   -> sigmoide, el modelo "dnn" de una capa oculta que entrena openWakeWord. */
static bool load_dense(WakeWord *w, const BlobTensor *ts, int n)
{
    const char *pre = "kw";
    char name[64];
    const BlobTensor *t;
    DenseNet *d = &w->net;
#define GET(layer, field)                                                  \
    snprintf(name, sizeof name, "%s.%s", pre, layer);                      \
    if (!(t = find(ts, n, name))) return false;                            \
    d->field = copy_floats(t);
#define GET_T(layer, field)                                                \
    snprintf(name, sizeof name, "%s.%s", pre, layer);                      \
    if (!(t = find(ts, n, name)) || t->ndim != 2) return false;            \
    d->field = transpose(t, 0);
#define SIZE(field, want)                                                  \
    if (t->count != (want)) return false;
    /* Tamaños exactos: el archivo puede venir de afuera (un entrenamiento). */
    snprintf(name, sizeof name, "%s.fc1.w", pre);
    if (!(t = find(ts, n, name)) || t->ndim != 2 || t->dims[1] != FEAT_FRAMES * EMB_DIM || t->dims[0] < 1 ||
        t->dims[0] > 256)
        return false;
    int h = d->hidden = t->dims[0];
    GET_T("fc1.w", fc1_wt)
    GET("fc1.b", fc1_b) SIZE(fc1_b, h)
    GET("ln1.w", ln1_w) SIZE(ln1_w, h)
    GET("ln1.b", ln1_b) SIZE(ln1_b, h)
    GET_T("fc2.w", fc2_wt) if (t->dims[0] != h || t->dims[1] != h) return false;
    GET("fc2.b", fc2_b) SIZE(fc2_b, h)
    GET("ln2.w", ln2_w) SIZE(ln2_w, h)
    GET("ln2.b", ln2_b) SIZE(ln2_b, h)
    GET_T("fc3.w", fc3_wt) if (t->dims[0] != 1 || t->dims[1] != h) return false;
    GET("fc3.b", fc3_b) SIZE(fc3_b, 1)
#undef GET
#undef GET_T
#undef SIZE
    return true;
}

static uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}

/* openWakeWord arranca su buffer de features con embeddings de 4 s de ruido
   aleatorio; solo importan los últimos 16 (lo que ve el clasificador), así
   que se calculan una vez y se reutilizan en cada reset. */
static void compute_noise_features(WakeWord *w)
{
    int n = 16000 * 4;
    float *noise = xmalloc(sizeof(float) * (size_t)n);
    uint32_t seed = 0x9E3779B9u;
    for (int i = 0; i < n; i++) noise[i] = (float)((int)(xorshift(&seed) % 2000) - 1000);
    int frames = (n - MEL_WIN) / MEL_HOP + 1;
    float *mel = xmalloc(sizeof(float) * (size_t)frames * MEL_N);
    ww_melspec(w, noise, n, mel);
    for (int i = 0; i < frames * MEL_N; i++) mel[i] = mel[i] / 10.0f + 2.0f;
    int windows = 0;
    for (int i = 0; i + EMB_FRAMES <= frames; i += 8) windows++;
    for (int k = 0; k < FEAT_FRAMES; k++) {
        int win = windows - FEAT_FRAMES + k;
        ww_embed(w, mel + (size_t)win * 8 * MEL_N, w->noise_feat[k]);
    }
    free(noise);
    free(mel);
}

WakeWord *ww_create(const void *blob, size_t len, const void *word, size_t word_len)
{
    BlobTensor *ts = NULL, *ws = NULL;
    int n = 0, wn = 0;
    if (!blob || !parse_blob(blob, len, &ts, &n)) {
        log_msg("wakeword: blob de pesos inválido");
        return NULL;
    }
    if (!word || !parse_blob(word, word_len, &ws, &wn)) {
        log_msg("wakeword: modelo de la palabra inválido");
        free(ts);
        return NULL;
    }
    WakeWord *w = xcalloc(1, sizeof *w);
    __builtin_cpu_init();
    bool avx2 = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
    w->conv = avx2 ? nn_conv_avx2 : nn_conv_generic;
    w->mat = avx2 ? nn_mat_t_avx2 : nn_mat_t_generic;

    const BlobTensor *re = find(ts, n, "mel.real"), *im = find(ts, n, "mel.imag"), *mw = find(ts, n, "mel.melW");
    if (!re || !im || !mw || re->dims[0] != MEL_BINS || re->dims[1] != MEL_WIN || mw->dims[0] != MEL_BINS ||
        mw->dims[1] != MEL_N)
        goto fail;
    w->mel_rt = transpose(re, MEL_BINS_PAD);
    w->mel_it = transpose(im, MEL_BINS_PAD);
    w->mel_w = copy_floats(mw);

    for (int i = 0; i < CONV_LAYERS; i++) {
        char name[64];
        snprintf(name, sizeof name, "emb.conv%d.w", i);
        const BlobTensor *t = find(ts, n, name);
        if (!t || t->ndim != 4) goto fail;
        ConvLayer *L = &w->layers[i];
        L->cout = t->dims[0];
        L->cin = t->dims[1];
        L->kh = t->dims[2];
        L->kw = t->dims[3];
        L->padw = L->kw == 3 ? 1 : 0;
        float *src = copy_floats(t);
        L->wt = xmalloc(sizeof(float) * (size_t)t->count);
        for (int o = 0; o < L->cout; o++)
            for (int c = 0; c < L->cin; c++)
                for (int a = 0; a < L->kh; a++)
                    for (int b = 0; b < L->kw; b++)
                        L->wt[(((size_t)a * L->kw + b) * L->cin + c) * L->cout + o] =
                            src[(((size_t)o * L->cin + c) * L->kh + a) * L->kw + b];
        free(src);
        snprintf(name, sizeof name, "emb.conv%d.b", i);
        const BlobTensor *bt = find(ts, n, name);
        L->b = bt ? copy_floats(bt) : NULL;
    }
    if (!load_dense(w, ws, wn)) goto fail;
    free(ts);
    free(ws);

    w->act_a = xmalloc(sizeof(float) * ACT_MAX);
    w->act_b = xmalloc(sizeof(float) * ACT_MAX);
    w->melbuf = xmalloc(sizeof(float) * (MEL_MAX + 16) * MEL_N);
    compute_noise_features(w);
    ww_reset(w);
    log_msg("Wake word listo (%s).", avx2 ? "AVX2" : "SSE2");
    return w;

fail:
    log_msg("wakeword: faltan tensores o tienen otra forma");
    free(ts);
    free(ws);
    ww_destroy(w);
    return NULL;
}

void ww_reset(WakeWord *w)
{
    w->raw_count = 0;
    w->mel_rows = EMB_FRAMES;
    for (int i = 0; i < EMB_FRAMES * MEL_N; i++) w->melbuf[i] = 1.0f;
    memcpy(w->feat, w->noise_feat, sizeof w->noise_feat);
    w->feat_rows = FEAT_FRAMES;
    w->predictions = 0;
}

int ww_melspec(WakeWord *w, const float *samples, int n, float *out)
{
    if (n < MEL_WIN) return 0;
    int frames = (n - MEL_WIN) / MEL_HOP + 1;
    float re[MEL_BINS_PAD], im[MEL_BINS_PAD], power[MEL_BINS], melv[MEL_N];
    float maxdb = -INFINITY;
    for (int f = 0; f < frames; f++) {
        const float *x = samples + (size_t)f * MEL_HOP;
        w->mat(x, MEL_WIN, w->mel_rt, NULL, MEL_BINS_PAD, re);
        w->mat(x, MEL_WIN, w->mel_it, NULL, MEL_BINS_PAD, im);
        for (int k = 0; k < MEL_BINS; k++) power[k] = re[k] * re[k] + im[k] * im[k];
        w->mat(power, MEL_BINS, w->mel_w, NULL, MEL_N, melv);
        for (int m = 0; m < MEL_N; m++) {
            float v = melv[m] < 1e-10f ? 1e-10f : melv[m];
            float db = (logf(v) * 10.0f) / 2.3025851249694824f;
            out[f * MEL_N + m] = db;
            if (db > maxdb) maxdb = db;
        }
    }
    float floor_db = maxdb - 80.0f;
    for (int i = 0; i < frames * MEL_N; i++)
        if (out[i] < floor_db) out[i] = floor_db;
    return frames;
}

void ww_embed(WakeWord *w, const float *mel, float *out96)
{
    int H = EMB_FRAMES, W = MEL_N, C = 1;
    const float *in = mel;
    float *bufs[2] = {w->act_a, w->act_b};
    int cur = 0;
    for (int i = 0; i < CONV_LAYERS; i++) {
        ConvLayer *L = &w->layers[i];
        float *out = bufs[cur];
        w->conv(in, H, W, C, L->wt, L->b, L->kh, L->kw, L->padw, L->cout, out, i < CONV_LAYERS - 1);
        H = H - L->kh + 1;
        W = W + 2 * L->padw - L->kw + 1;
        C = L->cout;
        in = out;
        cur ^= 1;
        int ph = POOL_AFTER[i][0], pw = POOL_AFTER[i][1];
        if (ph) {
            float *pout = bufs[cur];
            int Hp = H / ph, Wp = W / pw;
            for (int h = 0; h < Hp; h++)
                for (int x = 0; x < Wp; x++)
                    for (int c = 0; c < C; c++) {
                        float m = -INFINITY;
                        for (int a = 0; a < ph; a++)
                            for (int b = 0; b < pw; b++) {
                                float v = in[((size_t)(h * ph + a) * W + (x * pw + b)) * C + c];
                                if (v > m) m = v;
                            }
                        pout[((size_t)h * Wp + x) * C + c] = m;
                    }
            H = Hp;
            W = Wp;
            in = pout;
            cur ^= 1;
        }
    }
    memcpy(out96, in, sizeof(float) * EMB_DIM);
}

static void layer_norm_relu(float *x, int n, const float *g, const float *b)
{
    float mean = 0, var = 0;
    for (int i = 0; i < n; i++) mean += x[i];
    mean /= (float)n;
    for (int i = 0; i < n; i++) {
        float d = x[i] - mean;
        var += d * d;
    }
    var /= (float)n;
    float inv = 1.0f / sqrtf(var + 9.999999747378752e-06f);
    for (int i = 0; i < n; i++) {
        float v = (x[i] - mean) * inv * g[i] + b[i];
        x[i] = v > 0 ? v : 0;
    }
}

static float run_dense(WakeWord *w, const DenseNet *d, const float *x)
{
    float h1[256], h2[256], z;
    int n = d->hidden;
    w->mat(x, FEAT_FRAMES * EMB_DIM, d->fc1_wt, d->fc1_b, n, h1);
    layer_norm_relu(h1, n, d->ln1_w, d->ln1_b);
    w->mat(h1, n, d->fc2_wt, d->fc2_b, n, h2);
    layer_norm_relu(h2, n, d->ln2_w, d->ln2_b);
    w->mat(h2, n, d->fc3_wt, d->fc3_b, 1, &z);
    return 1.0f / (1.0f + expf(-z));
}

float ww_classify(WakeWord *w, const float *feat)
{
    return run_dense(w, &w->net, feat);
}

float ww_process(WakeWord *w, const int16_t *chunk)
{
    if (w->raw_count + WW_CHUNK > RAW_KEEP) {
        int drop = w->raw_count + WW_CHUNK - RAW_KEEP;
        memmove(w->raw, w->raw + drop, sizeof(float) * (size_t)(w->raw_count - drop));
        w->raw_count -= drop;
    }
    for (int i = 0; i < WW_CHUNK; i++) w->raw[w->raw_count + i] = (float)chunk[i];
    w->raw_count += WW_CHUNK;

    float frames_buf[16 * MEL_N];
    int frames = ww_melspec(w, w->raw, w->raw_count, frames_buf);
    if (w->mel_rows + frames > MEL_MAX) {
        int drop = w->mel_rows + frames - MEL_MAX;
        memmove(w->melbuf, w->melbuf + (size_t)drop * MEL_N, sizeof(float) * (size_t)(w->mel_rows - drop) * MEL_N);
        w->mel_rows -= drop;
    }
    for (int i = 0; i < frames * MEL_N; i++) w->melbuf[(size_t)w->mel_rows * MEL_N + i] = frames_buf[i] / 10.0f + 2.0f;
    w->mel_rows += frames;

    if (w->mel_rows >= EMB_FRAMES) {
        if (w->feat_rows == FEAT_MAX) {
            memmove(w->feat[0], w->feat[1], sizeof(float) * EMB_DIM * (FEAT_MAX - 1));
            w->feat_rows--;
        }
        ww_embed(w, w->melbuf + (size_t)(w->mel_rows - EMB_FRAMES) * MEL_N, w->feat[w->feat_rows]);
        w->feat_rows++;
    }

    float score = ww_classify(w, w->feat[w->feat_rows - FEAT_FRAMES]);
    if (w->predictions < 5) score = 0.0f;
    w->predictions++;
    return score;
}
