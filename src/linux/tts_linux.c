/* La voz de Sokari en Linux.
   - Piper: voces neuronales que corren en tu PC. La primera vez se bajan el
     programa (de su página en GitHub, revisado con su SHA-256) y una voz de
     México (del catálogo oficial de Piper, revisada con el tamaño y el MD5
     que publica ese catálogo). Se quedan en ~/.local/share/sokari.
   - espeak-ng: la voz básica que ya traen Ubuntu y Fedora. Es la que se usa
     mientras Piper se baja, o si no se pudo bajar.
   Piper se deja abierto con la voz cargada (tarda en cargarla) y se le pide
   cada frase en su propio archivo, así una frase nunca se mezcla con otra.
   Todas las funciones de tts.h se llaman desde el mismo hilo (el de voz); la
   descarga corre en otro y solo avisa cuando termina. */
#include <windows.h>

#include <ctype.h>
#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app.h"
#include "config.h"
#include "http.h"
#include "linux/linux.h"
#include "linux/proc.h"
#include "log.h"
#include "third_party/cJSON.h"
#include "tts.h"
#include "util.h"

#define PIPER_URL "https://github.com/rhasspy/piper/releases/download/2023.11.14-2/piper_linux_x86_64.tar.gz"
#define PIPER_SHA256 "a50cb45f355b7af1f6d758c1b360717877ba0a398cc8cbe6d2a7a3a26e225992"
#define VOICES_BASE "https://huggingface.co/rhasspy/piper-voices/resolve/main/"
#define ESPEAK_DEFAULT "espeak:es-419"

typedef enum { ENGINE_NONE, ENGINE_PIPER, ENGINE_ESPEAK } Engine;

static Engine g_engine;
static char *g_espeak;      /* ruta de espeak-ng, o NULL */
static char *g_espeak_lang; /* "es-419" */
static bool g_auto;         /* no escogiste voz: se usa la mejor que haya */
static volatile LONG g_piper_ready;
static HANDLE g_download;

static pid_t g_piper_pid = -1;
static int g_piper_in = -1, g_piper_out = -1;
static char *g_piper_key; /* la voz cargada: "es_MX-ald-medium" */
static char *g_tmpdir;
static unsigned g_seq;

/* ------------------------------------------------------------ carpetas --- */

static wchar_t *data_path(const wchar_t *rel)
{
    return path_join(g_paths.memory_dir, rel);
}

static char *piper_bin(void)
{
    wchar_t *p = data_path(L"piper/piper");
    char *r = wide_to_utf8(p);
    free(p);
    return r;
}

static char *voice_file(const char *key, const char *ext)
{
    wchar_t *dir = data_path(L"voces");
    char *d = wide_to_utf8(dir);
    free(dir);
    char *r = str_printf("%s/%s%s", d, key, ext);
    free(d);
    return r;
}

static bool exists8(const char *p)
{
    struct stat st;
    return !stat(p, &st) && S_ISREG(st.st_mode) && st.st_size > 0;
}

static bool piper_installed(void)
{
    char *b = piper_bin();
    bool ok = !access(b, X_OK);
    free(b);
    return ok;
}

/* Las voces de Piper que ya están bajadas (claves, heap). */
static int piper_voices(char ***out)
{
    wchar_t *dir = data_path(L"voces");
    char *d = wide_to_utf8(dir);
    free(dir);
    DIR *dh = opendir(d);
    int n = 0, cap = 4;
    char **list = xcalloc((size_t)cap, sizeof(char *));
    for (struct dirent *e; dh && (e = readdir(dh));) {
        size_t len = strlen(e->d_name);
        if (len <= 5 || strcmp(e->d_name + len - 5, ".onnx")) continue;
        char *key = xstrndup(e->d_name, len - 5);
        char *json = voice_file(key, ".onnx.json");
        if (exists8(json)) {
            if (n == cap) list = xrealloc(list, sizeof(char *) * (size_t)(cap *= 2));
            list[n++] = key;
        } else {
            free(key);
        }
        free(json);
    }
    if (dh) closedir(dh);
    free(d);
    *out = list;
    return n;
}

static void free_list(char **l, int n)
{
    for (int i = 0; i < n; i++) free(l[i]);
    free(l);
}

/* La mejor que haya: la de México hombre (ald), como en Windows se prefiere
   Raúl; si no, la de más calidad. */
static char *best_piper_voice(void)
{
    char **keys;
    int n = piper_voices(&keys);
    char *best = NULL;
    int best_score = -1;
    for (int i = 0; i < n; i++) {
        int score = (strstr(keys[i], "es_MX") ? 100 : 0) + (strstr(keys[i], "-ald-") ? 50 : 0) +
                    (strstr(keys[i], "-high") ? 4 : strstr(keys[i], "-medium") ? 3 : strstr(keys[i], "-low") ? 2 : 1);
        if (score > best_score) {
            best_score = score;
            free(best);
            best = xstrdup(keys[i]);
        }
    }
    free_list(keys, n);
    return best;
}

/* ----------------------------------------------------------------- WAV --- */

/* PCM de 16 bits mono de un WAV. espeak-ng por la salida estándar pone un
   tamaño falso (no sabe cuánto va a medir): se toma lo que hay. */
static int16_t *wav_pcm(const char *d, size_t len, int *rate, size_t *samples)
{
    *samples = 0;
    if (len < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return NULL;
    int channels = 1, bits = 16;
    *rate = 0;
    for (size_t off = 12; off + 8 <= len;) {
        uint32_t sz;
        memcpy(&sz, d + off + 4, 4);
        if (!memcmp(d + off, "fmt ", 4) && off + 24 <= len) {
            uint16_t ch, bps;
            uint32_t r;
            memcpy(&ch, d + off + 10, 2);
            memcpy(&r, d + off + 12, 4);
            memcpy(&bps, d + off + 22, 2);
            channels = ch, bits = bps, *rate = (int)r;
        } else if (!memcmp(d + off, "data", 4)) {
            size_t avail = len - off - 8;
            size_t bytes = sz < avail ? sz : avail;
            if (channels != 1 || bits != 16 || *rate <= 0) return NULL;
            int16_t *pcm = xmalloc(bytes + 2);
            memcpy(pcm, d + off + 8, bytes);
            *samples = bytes / 2;
            return pcm;
        }
        if (sz > len) break;
        off += 8 + sz + (sz & 1);
    }
    return NULL;
}

/* A TTS_RATE (24 kHz), que es a lo que habla Sokari en todos lados. */
static int16_t *resample(int16_t *in, size_t n, int rate, size_t *out_n)
{
    if (rate == TTS_RATE || !n) {
        *out_n = n;
        return in;
    }
    size_t m = (size_t)((double)n * TTS_RATE / rate);
    int16_t *out = xmalloc(sizeof(int16_t) * (m + 1));
    double step = (double)rate / TTS_RATE;
    for (size_t i = 0; i < m; i++) {
        double x = i * step;
        size_t k = (size_t)x;
        double f = x - (double)k;
        int a = in[k < n ? k : n - 1], b = in[k + 1 < n ? k + 1 : n - 1];
        out[i] = (int16_t)(a + (b - a) * f);
    }
    free(in);
    *out_n = m;
    return out;
}

/* ------------------------------------------------------------ espeak --- */

static int16_t *synth_espeak(const char *text, size_t *samples)
{
    *samples = 0;
    if (!g_espeak) return NULL;
    /* El texto por la entrada estándar, nunca como argumento: así nada de lo
       que diga puede tomarse como una opción. */
    const char *argv[] = {g_espeak, "-v", g_espeak_lang ? g_espeak_lang : "es-419", "-s", "165", "--stdin", "--stdout",
                          NULL};
    size_t len = 0;
    int code;
    char *wav = proc_run(argv, text, strlen(text), 20000, 64u << 20, &len, &code);
    int rate = 0;
    int16_t *pcm = wav && code == 0 ? wav_pcm(wav, len, &rate, samples) : NULL;
    free(wav);
    if (!pcm) {
        log_msg("espeak-ng no pudo decir la frase.");
        return NULL;
    }
    return resample(pcm, *samples, rate, samples);
}

/* ------------------------------------------------------------- Piper --- */

static void piper_stop(void)
{
    if (g_piper_in >= 0) close(g_piper_in);
    if (g_piper_out >= 0) close(g_piper_out);
    g_piper_in = g_piper_out = -1;
    if (g_piper_pid > 0) proc_finish(g_piper_pid, 1500);
    g_piper_pid = -1;
}

static bool piper_start(const char *key)
{
    piper_stop();
    char *bin = piper_bin(), *model = voice_file(key, ".onnx");
    const char *argv[] = {bin, "--model", model, "--json-input", "--quiet", NULL};
    g_piper_pid = exists8(model) && !access(bin, X_OK) ? proc_spawn(argv, &g_piper_in, &g_piper_out, NULL, NULL) : -1;
    free(bin);
    free(model);
    if (g_piper_pid < 0) {
        g_piper_in = g_piper_out = -1;
        return false;
    }
    free(g_piper_key);
    g_piper_key = xstrdup(key);
    if (!g_tmpdir) {
        const char *rt = getenv("XDG_RUNTIME_DIR");
        char *tmpl = str_printf("%s/sokari-voz-XXXXXX", rt && *rt ? rt : "/tmp");
        g_tmpdir = mkdtemp(tmpl) ? tmpl : NULL; /* 0700: solo tuyo */
        if (!g_tmpdir) free(tmpl);
    }
    return g_tmpdir != NULL;
}

static int16_t *synth_piper(const char *text, size_t *samples)
{
    *samples = 0;
    if (g_piper_pid < 0 || !g_tmpdir) return NULL;
    char *path = str_printf("%s/%u.wav", g_tmpdir, ++g_seq);
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "text", text);
    cJSON_AddStringToObject(o, "output_file", path);
    char *line = cJSON_PrintUnformatted(o); /* una sola línea: los saltos van como \n */
    cJSON_Delete(o);
    char *nl = str_printf("%s\n", line);
    free(line);
    bool sent = proc_write_all(g_piper_in, nl, strlen(nl));
    free(nl);
    /* Piper escribe el archivo y luego avisa con su ruta. */
    char *done = sent ? proc_read_line(g_piper_out, 30000) : NULL;
    int16_t *pcm = NULL;
    if (done) {
        wchar_t *w = utf8_to_wide(path);
        size_t len = 0;
        char *wav = read_file_all(w, &len);
        int rate = 0;
        if (wav) pcm = wav_pcm(wav, len, &rate, samples);
        if (pcm) pcm = resample(pcm, *samples, rate, samples);
        free(wav);
        free(w);
    }
    unlink(path);
    free(path);
    free(done);
    return pcm;
}

/* ------------------------------------------------------ bajar Piper --- */

/* MD5 (el que publica el catálogo de voces de Piper). */
static void md5(const unsigned char *msg, size_t len, unsigned char out[16])
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                              14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t h[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    size_t total = ((len + 8) / 64 + 1) * 64;
    unsigned char *m = xcalloc(total, 1);
    memcpy(m, msg, len);
    m[len] = 0x80;
    uint64_t bits = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) m[total - 8 + i] = (unsigned char)(bits >> (8 * i));
    for (size_t off = 0; off < total; off += 64) {
        uint32_t w[16];
        for (int i = 0; i < 16; i++)
            w[i] = (uint32_t)m[off + i * 4] | (uint32_t)m[off + i * 4 + 1] << 8 | (uint32_t)m[off + i * 4 + 2] << 16 |
                   (uint32_t)m[off + i * 4 + 3] << 24;
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) f = (b & c) | (~b & d), g = i;
            else if (i < 32) f = (d & b) | (~d & c), g = (5 * i + 1) % 16;
            else if (i < 48) f = b ^ c ^ d, g = (3 * i + 5) % 16;
            else f = c ^ (b | ~d), g = (7 * i) % 16;
            uint32_t t = d;
            d = c;
            c = b;
            uint32_t x = a + f + K[i] + w[g];
            b = b + ((x << R[i]) | (x >> (32 - R[i])));
            a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d;
    }
    free(m);
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) out[i * 4 + k] = (unsigned char)(h[i] >> (8 * k));
}

char *tts_md5_hex(const void *data, size_t n)
{
    unsigned char dg[16];
    md5(data, n, dg);
    return hex_encode(dg, 16);
}

/* Baja url a dest (vía un .part) y lo deja solo si pesa y se ve como debe:
   sha256 o md5 (hex) si se dan, y el tamaño si size > 0. */
static bool fetch_checked(const char *url, const char *dest, const char *sha256, const char *md5hex, long long size)
{
    char *part = str_printf("%s.part", dest);
    wchar_t *wpart = utf8_to_wide(part), *wdest = utf8_to_wide(dest);
    HttpRequest req = {.method = "GET", .url = url, .timeout_ms = 60000, .download_to = wpart};
    HttpResponse r = http_request(&req);
    bool ok = r.status == 200 && !r.error;
    if (!ok) log_msg("No pude bajar %s: %s", url, r.error ? r.error : "respuesta inesperada");
    http_response_free(&r);
    size_t len = 0;
    char *data = ok ? read_file_all(wpart, &len) : NULL;
    if (ok && (!data || (size > 0 && (long long)len != size))) {
        log_msg("La descarga de %s no pesa lo que debe; la descarto.", url);
        ok = false;
    }
    if (ok && sha256) {
        unsigned char dg[32];
        sha256_bytes(data, len, dg);
        char *hex = hex_encode(dg, 32);
        ok = !strcmp(hex, sha256);
        if (!ok) log_msg("La descarga de %s no coincide con su SHA-256; la descarto.", url);
        free(hex);
    }
    if (ok && md5hex) {
        char *hex = tts_md5_hex(data, len);
        ok = !strcmp(hex, md5hex);
        if (!ok) log_msg("La descarga de %s no coincide con su MD5; la descarto.", url);
        free(hex);
    }
    free(data);
    if (ok) ok = move_file(wpart, wdest);
    else DeleteFileW(wpart);
    free(wpart);
    free(wdest);
    free(part);
    return ok;
}

static bool install_piper(void)
{
    if (piper_installed()) return true;
    char *dir = wide_to_utf8(g_paths.memory_dir);
    char *tgz = str_printf("%s/piper.tar.gz", dir);
    bool ok = fetch_checked(PIPER_URL, tgz, PIPER_SHA256, NULL, 0);
    if (ok) {
        const char *argv[] = {"tar", "-xzf", tgz, "-C", dir, NULL};
        int code;
        free(proc_run(argv, NULL, 0, 120000, 0, NULL, &code));
        ok = code == 0 && piper_installed();
        if (!ok) log_msg("No pude desempacar Piper.");
    }
    unlink(tgz);
    free(tgz);
    free(dir);
    return ok;
}

/* Del catálogo de voces de Piper, la de México que se va a bajar: la de
   hombre (ald), como en Windows; si no hay, la de más calidad. Aparte para
   poder probarla sin internet. */
bool tts_pick_catalog_voice(const char *catalog_json, char **key, char **onnx_path, char **onnx_md5, long long *onnx_size,
                            char **json_path, char **json_md5, long long *json_size)
{
    cJSON *root = cJSON_Parse(catalog_json);
    const cJSON *best = NULL;
    int best_score = -1;
    const cJSON *v;
    cJSON_ArrayForEach(v, root)
    {
        const char *code = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(v, "language"), "code"));
        const char *quality = cJSON_GetStringValue(cJSON_GetObjectItem(v, "quality"));
        if (!v->string || !code || strcmp(code, "es_MX")) continue;
        int score = (strstr(v->string, "-ald-") ? 50 : 0) + (!quality                    ? 0
                                                              : !strcmp(quality, "high")   ? 4
                                                              : !strcmp(quality, "medium") ? 3
                                                              : !strcmp(quality, "low")    ? 2
                                                                                           : 1);
        if (score > best_score) best_score = score, best = v;
    }
    bool ok = false;
    if (best) {
        *key = xstrdup(best->string);
        *onnx_path = *json_path = *onnx_md5 = *json_md5 = NULL;
        const cJSON *f;
        cJSON_ArrayForEach(f, cJSON_GetObjectItem(best, "files"))
        {
            const char *md5v = cJSON_GetStringValue(cJSON_GetObjectItem(f, "md5_digest"));
            const cJSON *size = cJSON_GetObjectItem(f, "size_bytes");
            if (!f->string || !md5v || !cJSON_IsNumber(size)) continue;
            if (str_ends_with(f->string, ".onnx")) {
                free(*onnx_path), free(*onnx_md5);
                *onnx_path = xstrdup(f->string), *onnx_md5 = xstrdup(md5v), *onnx_size = (long long)size->valuedouble;
            } else if (str_ends_with(f->string, ".onnx.json")) {
                free(*json_path), free(*json_md5);
                *json_path = xstrdup(f->string), *json_md5 = xstrdup(md5v), *json_size = (long long)size->valuedouble;
            }
        }
        ok = *onnx_path && *json_path;
        if (!ok) {
            free(*key), free(*onnx_path), free(*json_path), free(*onnx_md5), free(*json_md5);
            *key = *onnx_path = *json_path = *onnx_md5 = *json_md5 = NULL;
        }
    }
    cJSON_Delete(root);
    return ok;
}

static bool install_voice(void)
{
    char *have = best_piper_voice();
    if (have && strstr(have, "es_MX")) {
        free(have);
        return true;
    }
    free(have);
    HttpRequest req = {.method = "GET", .url = VOICES_BASE "voices.json", .timeout_ms = 30000, .max_bytes = 16u << 20};
    HttpResponse r = http_request(&req);
    char *key = NULL, *onnx = NULL, *onnx_md5 = NULL, *json = NULL, *json_md5 = NULL;
    long long onnx_size = 0, json_size = 0;
    bool ok = r.status == 200 &&
              tts_pick_catalog_voice(r.body, &key, &onnx, &onnx_md5, &onnx_size, &json, &json_md5, &json_size);
    if (!ok) log_msg("No encontré una voz de México en el catálogo de Piper (%s).", r.error ? r.error : "sin la lista");
    http_response_free(&r);
    if (ok) {
        wchar_t *dir = data_path(L"voces");
        ensure_dir(dir);
        free(dir);
        char *url_json = str_printf("%s%s", VOICES_BASE, json), *url_onnx = str_printf("%s%s", VOICES_BASE, onnx);
        char *dst_json = voice_file(key, ".onnx.json"), *dst_onnx = voice_file(key, ".onnx");
        /* Primero el modelo (lo pesado) y al final su configuración: una voz
           solo "existe" con las dos. */
        ok = fetch_checked(url_onnx, dst_onnx, NULL, onnx_md5, onnx_size) &&
             fetch_checked(url_json, dst_json, NULL, json_md5, json_size);
        if (ok) log_msg("Voz de Piper lista: %s", key);
        free(url_json), free(url_onnx), free(dst_json), free(dst_onnx);
    }
    free(key), free(onnx), free(onnx_md5), free(json), free(json_md5);
    return ok;
}

static DWORD WINAPI download_thread(LPVOID arg)
{
    if (install_piper() && install_voice()) {
        InterlockedExchange(&g_piper_ready, 1);
        app_notify("Sokari", "Ya tengo una voz más natural (Piper, de México). Si prefieres otra, está en Configuración.");
    }
    return 0;
}

/* "es_MX-ald-medium" -> "Ald (México, Piper)". Heap. */
char *tts_piper_voice_name(const char *key)
{
    static const char *const QUALITY[] = {"-x_low", "-x-low", "-low", "-medium", "-high"};
    static const struct {
        const char *code, *name;
    } PLACES[] = {{"es_MX", "México"}, {"es_ES", "España"}, {"es_AR", "Argentina"}, {"es", "español"}};
    char *k = xstrdup(key);
    for (size_t i = 0; i < sizeof QUALITY / sizeof *QUALITY; i++)
        if (str_ends_with(k, QUALITY[i])) {
            k[strlen(k) - strlen(QUALITY[i])] = 0;
            break;
        }
    char *dash = strchr(k, '-');
    char *r;
    if (dash && dash[1]) {
        *dash = 0;
        const char *place = k;
        for (size_t i = 0; i < sizeof PLACES / sizeof *PLACES; i++)
            if (!strcmp(k, PLACES[i].code)) place = PLACES[i].name;
        dash[1] = (char)toupper((unsigned char)dash[1]);
        r = str_printf("%s (%s, Piper)", dash + 1, place);
    } else {
        r = str_printf("%s (Piper)", k);
    }
    free(k);
    return r;
}

/* ------------------------------------------------------------- tts.h --- */

int tts_list_voices(TtsVoice **out)
{
    char **keys;
    int n = piper_voices(&keys);
    TtsVoice *list = xcalloc((size_t)n + 3, sizeof *list);
    int k = 0;
    if (piper_installed()) {
        for (int i = 0; i < n; i++) {
            list[k].id = str_printf("piper:%s", keys[i]);
            list[k].name = tts_piper_voice_name(keys[i]);
            k++;
        }
    }
    free_list(keys, n);
    if (g_espeak || (g_espeak = proc_which("espeak-ng"))) {
        list[k].id = xstrdup(ESPEAK_DEFAULT);
        list[k++].name = xstrdup("Latinoamericana básica (espeak-ng)");
        list[k].id = xstrdup("espeak:es");
        list[k++].name = xstrdup("España básica (espeak-ng)");
    }
    *out = list;
    return k;
}

void tts_free_voices(TtsVoice *v, int n)
{
    for (int i = 0; i < n; i++) {
        free(v[i].id);
        free(v[i].name);
    }
    free(v);
}

bool tts_set_voice(const char *id)
{
    if (!id || !*id) return false;
    if (str_starts_with(id, "piper:")) {
        if (!piper_start(id + 6)) return false;
        g_engine = ENGINE_PIPER;
        log_msg("Voz: Piper %s", id + 6);
        return true;
    }
    if (str_starts_with(id, "espeak:") && g_espeak) {
        piper_stop();
        free(g_espeak_lang);
        g_espeak_lang = xstrdup(id + 7);
        g_engine = ENGINE_ESPEAK;
        log_msg("Voz: espeak-ng %s", id + 7);
        return true;
    }
    return false;
}

static bool use_best(void)
{
    char *best = piper_installed() ? best_piper_voice() : NULL;
    bool ok = false;
    if (best) {
        char *id = str_printf("piper:%s", best);
        ok = tts_set_voice(id);
        free(id);
        free(best);
    }
    return ok || tts_set_voice(ESPEAK_DEFAULT);
}

bool tts_init(const char *preferred_voice)
{
    signal(SIGPIPE, SIG_IGN); /* si Piper se cae a media frase, que no nos tumbe */
    g_espeak = proc_which("espeak-ng");
    /* Sin voz escogida (o una de Windows, si copiaste tu configuración): la
       mejor que haya, y Piper en cuanto se baje. */
    g_auto = !preferred_voice || (!str_starts_with(preferred_voice, "piper:") && !str_starts_with(preferred_voice, "espeak:"));
    bool ok = (!g_auto && tts_set_voice(preferred_voice)) || use_best();
    char *best = piper_installed() ? best_piper_voice() : NULL;
    bool have_piper = best != NULL;
    free(best);
    /* Las pruebas no bajan nada (SOKARI_SIN_DESCARGAS=1). */
    if (!have_piper && !getenv("SOKARI_SIN_DESCARGAS")) {
        if (!g_download) {
            log_msg("Bajando la voz de Piper (unos 90 MB, una sola vez); mientras, hablo con espeak-ng.");
            g_download = CreateThread(NULL, 0, download_thread, NULL, 0, NULL);
        }
    } else {
        InterlockedExchange(&g_piper_ready, 1);
    }
    if (!ok) {
        log_msg("No tengo con qué hablar: falta espeak-ng (sudo apt install espeak-ng / sudo dnf install espeak-ng).");
        app_notify("Sokari", "No tengo voz todavía: instala espeak-ng, o espera a que termine de bajar la de Piper.");
    }
    return ok;
}

void tts_shutdown(void)
{
    piper_stop();
    if (g_tmpdir) {
        rmdir(g_tmpdir);
        free(g_tmpdir);
        g_tmpdir = NULL;
    }
    g_engine = ENGINE_NONE;
}

int16_t *tts_synthesize(const char *text, size_t *samples)
{
    *samples = 0;
    if (str_is_blank(text)) return NULL;
    /* Terminó de bajar Piper y no escogiste voz: desde ahora habla con él. */
    if (g_auto && g_engine != ENGINE_PIPER && InterlockedCompareExchange(&g_piper_ready, 1, 1)) use_best();
    if (g_engine == ENGINE_NONE && !use_best()) return NULL;
    int16_t *pcm = NULL;
    if (g_engine == ENGINE_PIPER) {
        pcm = synth_piper(text, samples);
        if (!pcm && g_piper_key) {
            /* Se cayó: se levanta otra vez y se intenta una vez más. */
            char *key = xstrdup(g_piper_key);
            log_msg("Piper no contestó; lo reinicio.");
            if (piper_start(key)) pcm = synth_piper(text, samples);
            free(key);
        }
        if (!pcm) pcm = synth_espeak(text, samples);
    } else {
        pcm = synth_espeak(text, samples);
    }
    return pcm;
}
