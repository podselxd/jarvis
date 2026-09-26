/* La voz en Linux: qué voz de Piper se baja del catálogo y cómo se revisa,
   cómo se llaman las voces, espeak-ng, Piper si ya está bajado, y el audio
   con el servidor de sonido (PulseAudio o PipeWire): sonar, cortar a media
   frase, el micrófono y bajar/regresar el volumen. Lo que necesita algo que
   esta máquina no tiene (espeak-ng, un servidor de sonido, Piper bajado) lo
   dice y no lo cuenta. */
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio.h"
#include "config.h"
#include "linux/linux.h"
#include "linux/proc.h"
#include "tts.h"
#include "util.h"

static int g_fail, g_total;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) g_fail++;
}

static void note(const char *what)
{
    printf("      (%s)\n", what);
}

static const char *CATALOG =
    "{\"es_ES-davefx-medium\":{\"key\":\"es_ES-davefx-medium\",\"language\":{\"code\":\"es_ES\"},\"quality\":\"medium\","
    "\"files\":{\"es/es_ES/davefx/medium/es_ES-davefx-medium.onnx\":{\"size_bytes\":63201294,\"md5_digest\":\"aa\"},"
    "\"es/es_ES/davefx/medium/es_ES-davefx-medium.onnx.json\":{\"size_bytes\":4882,\"md5_digest\":\"bb\"}}},"
    "\"es_MX-claude-high\":{\"key\":\"es_MX-claude-high\",\"language\":{\"code\":\"es_MX\"},\"quality\":\"high\","
    "\"files\":{\"es/es_MX/claude/high/es_MX-claude-high.onnx\":{\"size_bytes\":114199011,\"md5_digest\":\"cc\"},"
    "\"es/es_MX/claude/high/es_MX-claude-high.onnx.json\":{\"size_bytes\":7033,\"md5_digest\":\"dd\"},"
    "\"es/es_MX/claude/high/MODEL_CARD\":{\"size_bytes\":279,\"md5_digest\":\"ee\"}}},"
    "\"es_MX-ald-medium\":{\"key\":\"es_MX-ald-medium\",\"language\":{\"code\":\"es_MX\"},\"quality\":\"medium\","
    "\"files\":{\"es/es_MX/ald/medium/es_MX-ald-medium.onnx.json\":{\"size_bytes\":4897,\"md5_digest\":\"ff\"},"
    "\"es/es_MX/ald/medium/es_MX-ald-medium.onnx\":{\"size_bytes\":63201294,\"md5_digest\":\"0123abcd\"}}}}";

static void test_catalogo(void)
{
    printf("-- qué voz de Piper se baja --\n");
    char *key, *onnx, *omd5, *json, *jmd5;
    long long osize = 0, jsize = 0;
    bool ok = tts_pick_catalog_voice(CATALOG, &key, &onnx, &omd5, &osize, &json, &jmd5, &jsize);
    check(ok && !strcmp(key, "es_MX-ald-medium"), "la de México hombre (ald), como en Windows se prefiere Raúl");
    check(ok && !strcmp(onnx, "es/es_MX/ald/medium/es_MX-ald-medium.onnx") && osize == 63201294 &&
              !strcmp(omd5, "0123abcd"),
          "con la ruta, el tamaño y el MD5 del modelo que publica el catálogo");
    check(ok && !strcmp(json, "es/es_MX/ald/medium/es_MX-ald-medium.onnx.json") && jsize == 4897 && !strcmp(jmd5, "ff"),
          "y los de su configuración");
    if (ok) free(key), free(onnx), free(omd5), free(json), free(jmd5);
    const char *no_mx = "{\"es_ES-davefx-medium\":{\"language\":{\"code\":\"es_ES\"},\"quality\":\"medium\",\"files\":{}}}";
    check(!tts_pick_catalog_voice(no_mx, &key, &onnx, &omd5, &osize, &json, &jmd5, &jsize),
          "sin voz de México no se baja otra cualquiera");
    check(!tts_pick_catalog_voice("<html>no es json</html>", &key, &onnx, &omd5, &osize, &json, &jmd5, &jsize),
          "si el catálogo no se entiende, no se baja nada");
    const char *no_md5 = "{\"es_MX-ald-medium\":{\"language\":{\"code\":\"es_MX\"},\"quality\":\"medium\",\"files\":{"
                         "\"a/es_MX-ald-medium.onnx\":{\"size_bytes\":10},\"a/es_MX-ald-medium.onnx.json\":{\"size_bytes\":5}}}}";
    check(!tts_pick_catalog_voice(no_md5, &key, &onnx, &omd5, &osize, &json, &jmd5, &jsize),
          "sin MD5 para revisarla, tampoco");

    char *h = tts_md5_hex("", 0);
    bool m1 = !strcmp(h, "d41d8cd98f00b204e9800998ecf8427e");
    free(h);
    h = tts_md5_hex("abc", 3);
    bool m2 = !strcmp(h, "900150983cd24fb0d6963f7d28e17f72");
    free(h);
    char a100[100];
    memset(a100, 'a', sizeof a100);
    h = tts_md5_hex(a100, sizeof a100);
    bool m3 = !strcmp(h, "36a92cc94a9e0fa21f625f8bfb007adf");
    free(h);
    check(m1 && m2 && m3, "MD5 correcto (vacío, «abc» y 100 bytes)");

    char *n = tts_piper_voice_name("es_MX-ald-medium");
    check(!strcmp(n, "Ald (México, Piper)"), n);
    free(n);
    n = tts_piper_voice_name("es_ES-davefx-x_low");
    check(!strcmp(n, "Davefx (España, Piper)"), n);
    free(n);
}

static void test_voces(void)
{
    printf("-- las voces --\n");
    tts_init(NULL);
    TtsVoice *v;
    int n = tts_list_voices(&v);
    bool espeak = false, piper = false;
    for (int i = 0; i < n; i++) {
        printf("      voz: %s (%s)\n", v[i].name, v[i].id);
        espeak |= str_starts_with(v[i].id, "espeak:");
        piper |= str_starts_with(v[i].id, "piper:");
    }
    tts_free_voices(v, n);
    char *has_espeak = proc_which("espeak-ng");
    check(!has_espeak || espeak, "si hay espeak-ng, aparece en la lista");
    if (has_espeak) {
        check(tts_set_voice("espeak:es-419"), "se puede escoger espeak-ng");
        size_t samples = 0;
        int16_t *pcm = tts_synthesize("Hola, soy Sokari. ¿Qué necesitas?", &samples);
        double secs = samples / (double)TTS_RATE;
        char what[120];
        snprintf(what, sizeof what, "espeak-ng dice la frase (%.1f s de audio, a 24 kHz)", secs);
        check(pcm && secs > 1.0 && secs < 8.0, what);
        free(pcm);
        pcm = tts_synthesize("-v en --help", &samples);
        check(pcm && samples > 0, "un texto que parece opción se dice, no se toma como opción");
        free(pcm);
    } else {
        note("sin espeak-ng en esta máquina: no se prueba su voz");
    }
    free(has_espeak);
    if (piper) {
        TtsVoice *pv;
        int m = tts_list_voices(&pv);
        const char *id = NULL;
        for (int i = 0; i < m && !id; i++)
            if (str_starts_with(pv[i].id, "piper:")) id = pv[i].id;
        check(tts_set_voice(id), "se puede escoger la voz de Piper");
        size_t samples = 0;
        int16_t *pcm = tts_synthesize("Hola, soy Sokari.", &samples);
        check(pcm && samples > TTS_RATE / 2, "Piper dice la frase");
        free(pcm);
        pcm = tts_synthesize("Y otra frase, con\nun salto de línea.", &samples);
        check(pcm && samples > TTS_RATE / 2, "y la siguiente, sin volver a cargar la voz");
        free(pcm);
        tts_free_voices(pv, m);
    } else {
        note("Piper no está bajado aquí: no se prueba su voz");
    }
    tts_shutdown();
}

typedef struct {
    int calls, stop_after;
    float max_level, last;
} PlayLog;

static bool play_cb(float level, void *ctx)
{
    PlayLog *p = ctx;
    p->calls++;
    p->last = level;
    if (level > p->max_level) p->max_level = level;
    return !p->stop_after || p->calls < p->stop_after;
}

static int16_t *tone(int rate, double secs, size_t *n)
{
    *n = (size_t)(rate * secs);
    int16_t *pcm = xmalloc(sizeof(int16_t) * *n);
    for (size_t i = 0; i < *n; i++) pcm[i] = (int16_t)(8000 * __builtin_sin(2 * 3.14159265 * 440 * (double)i / rate));
    return pcm;
}

static void test_audio(void)
{
    printf("-- el audio (servidor de sonido) --\n");
    char **outs;
    int nout = speaker_list_devices(&outs);
    for (int i = 0; i < nout; i++) printf("      salida: %s\n", outs[i]);
    size_t n;
    int16_t *pcm = tone(24000, 1.0, &n);
    PlayLog log = {0};
    uint64_t t0 = now_ms();
    bool played = speaker_play(pcm, n, 24000, 1.0f, play_cb, &log);
    uint64_t took = now_ms() - t0;
    if (!played && !log.calls) {
        note("sin servidor de sonido en esta máquina: no se prueba el audio");
        free(pcm);
        free_string_list(outs, nout);
        return;
    }
    char what[160];
    /* Cada servidor tarda distinto en arrancar un sonido (el de prueba de la
       CI, casi un segundo): se revisa que no se apresure y, abajo, que cortar
       ahorre tiempo contra dejarlo sonar. */
    snprintf(what, sizeof what, "un tono de 1 s suena completo, a su ritmo (tardó %llu ms, %d avisos a la esfera)",
             (unsigned long long)took, log.calls);
    check(played && took >= 950 && log.calls >= 15, what);
    uint64_t full_ms = took;
    check(log.max_level > 0.5f && log.last == 0.0f, "la esfera recibe el nivel de lo que suena, y al final un 0");
    if (nout) {
        speaker_set_device(outs[nout - 1]);
        PlayLog l2 = {0};
        check(speaker_play(pcm, n / 4, 24000, 0.5f, play_cb, &l2), "también por una salida escogida por su nombre");
        speaker_set_device(NULL);
    }
    PlayLog cut = {.stop_after = 5};
    t0 = now_ms();
    bool full = speaker_play(pcm, n, 24000, 1.0f, play_cb, &cut);
    took = now_ms() - t0;
    snprintf(what, sizeof what, "cortar a media frase (un clic en la esfera) se oye al momento (%llu ms contra %llu)",
             (unsigned long long)took, (unsigned long long)full_ms);
    check(!full && took + 500 < full_ms, what);
    free(pcm);
    free_string_list(outs, nout);

    check(mic_start(""), "abre el micrófono predeterminado");
    int16_t frame[MIC_FRAME];
    /* El ritmo se cuenta desde el primer pedazo: lo que tarda el servidor de
       sonido en arrancar el micrófono (el de prueba, a veces más de un
       segundo) no es lo que se mide aquí. */
    t0 = now_ms();
    bool first = false;
    while (!first && now_ms() - t0 < 3000) first = mic_read(frame, 200);
    t0 = now_ms();
    int frames = 0;
    while (first && now_ms() - t0 < 2000)
        if (mic_read(frame, 200)) frames++;
    snprintf(what, sizeof what, "llega audio del micrófono a su ritmo (%d pedazos de 80 ms en 2 s)", frames);
    check(frames >= 10 && frames <= 27, what);
    mic_stop();

    DuckState d = system_duck(0.3f);
    if (d.before < 0) {
        note("la salida está en silencio: no hay volumen que bajar");
        return;
    }
    check(d.ducked > 0 && d.ducked < d.before, "baja el volumen de la PC mientras te escucha");
    system_unduck(d);
    DuckState again = system_duck(1.0f);
    check(again.before > d.before - 0.02f && again.before < d.before + 0.02f, "y lo regresa como estaba");
    system_unduck(again);
    /* Si mientras te escuchaba le moviste tú al volumen, se queda como lo dejaste. */
    d = system_duck(0.3f);
    const char *argv[] = {"pactl", "set-sink-volume", "@DEFAULT_SINK@", "55%", NULL};
    int code;
    free(proc_run(argv, NULL, 0, 5000, 0, NULL, &code));
    if (code == 0) {
        system_unduck(d);
        DuckState now = system_duck(1.0f);
        check(now.before > 0.53f && now.before < 0.57f, "si le moviste tú mientras tanto, no te lo cambia");
        system_unduck(now);
        const char *back[] = {"pactl", "set-sink-volume", "@DEFAULT_SINK@", "100%", NULL};
        free(proc_run(back, NULL, 0, 5000, 0, NULL, &code));
    } else {
        system_unduck(d);
        note("sin pactl: no se prueba el caso de que muevas el volumen");
    }
}

int main(void)
{
    paths_init();
    config_load();
    test_catalogo();
    test_voces();
    test_audio();
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
