/* Sokari en Linux. Por ahora sin ventanas: en modo voz (sokari --voz) te
   escucha como en Windows ("Hey Sokari") y te contesta hablando, con los
   avisos en la terminal; en modo texto (sokari --texto) le escribes. El mismo
   agente, las mismas herramientas y las mismas confirmaciones que en
   Windows. La interfaz llega en la parte 6. */
#include <windows.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "audio.h"
#include "config.h"
#include "http.h"
#include "linux/linux.h"
#include "log.h"
#include "memory.h"
#include "util.h"
#include "voice.h"

static void usage(void)
{
    printf("Sokari %s para Linux\n\n"
           "  sokari --voz               escucharte (\"Hey Sokari\") y contestarte hablando; Ctrl+C para salir\n"
           "  sokari --texto             platicar escribiendo (Ctrl+D para salir)\n"
           "  sokari --simular x.wav     como --voz, pero con ese audio (16 kHz) en vez del micrófono\n"
           "  sokari --version           la versión\n\n"
           "La configuración está en ~/.config/sokari/config.env (tu API key de Groq va en\n"
           "GROQ_API_KEY=...). La memoria y tus datos, en ~/.local/share/sokari.\n",
           SOKARI_VERSION);
}

static bool has_groq_key(void)
{
    AppConfig cfg = config_snapshot();
    bool has = *cfg.groq_api_key != 0;
    SecureZeroMemory(cfg.groq_api_key, strlen(cfg.groq_api_key));
    config_free(&cfg);
    return has;
}

static int run_text(void)
{
    if (!has_groq_key()) {
        char *path = wide_to_utf8(g_paths.config_file);
        printf("Falta tu API key de Groq. Sácala gratis en https://console.groq.com/keys y ponla en\n"
               "%s así:\n\n  GROQ_API_KEY=gsk_...\n\n",
               path);
        free(path);
        return 2;
    }
    agent_init();
    printf("Sokari %s (modo texto). Escribe lo que le dirías; Ctrl+D para salir.\n", SOKARI_VERSION);
    Conversation *c = conv_create(true);
    char line[4096];
    for (;;) {
        printf("\ntú> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) break;
        char *text = str_trim(line);
        if (!*text) {
            free(text);
            continue;
        }
        TurnResult r = agent_process(c, text);
        free(text);
        if (r.reply && *r.reply) printf("sokari> %s\n", r.reply);
        free(r.reply);
        if (r.shutdown || app_quit_requested()) break;
        if (!r.keep_going) {
            /* Como cuando te despides en voz: la siguiente vez empieza de nuevo. */
            conv_destroy(c);
            c = conv_create(true);
        }
    }
    conv_destroy(c);
    printf("\n");
    return 0;
}

static volatile sig_atomic_t g_sigint;

static void on_sigint(int sig)
{
    (void)sig;
    g_sigint = 1;
}

static int run_voice(const char *wav)
{
    if (!has_groq_key()) return run_text(); /* dice dónde poner la key */
    log_to_console(1);
    if (wav) {
        /* La simulación nunca toca tu memoria real: usa una carpeta temporal. */
        char tmpl[] = "/tmp/sokari-simulacion-XXXXXX";
        if (mkdtemp(tmpl)) {
            free(g_paths.memory_dir);
            g_paths.memory_dir = utf8_to_wide(tmpl);
        }
        wchar_t *w = utf8_to_wide(wav);
        voice_set_input_wav(w);
        free(w);
    } else {
        AppConfig cfg = config_snapshot();
        speaker_set_device(cfg.output_name);
        config_free(&cfg);
        voice_mesh_start();
    }
    struct sigaction sa = {0};
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    if (!voice_start()) return 1;
    while (!voice_wait(200)) {
        if (g_sigint || app_quit_requested()) {
            voice_stop();
            break;
        }
    }
    if (!wav) voice_mesh_stop();
    return 0;
}

int main(int argc, char **argv)
{
    bool text = false, voice = false;
    const char *wav = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--texto")) {
            text = true;
        } else if (!strcmp(argv[i], "--voz")) {
            voice = true;
        } else if (!strcmp(argv[i], "--simular") && i + 1 < argc) {
            wav = argv[++i];
        } else if (!strcmp(argv[i], "--version")) {
            printf("%s\n", SOKARI_VERSION);
            return 0;
        } else {
            usage();
            return strcmp(argv[i], "--ayuda") && strcmp(argv[i], "--help") ? 1 : 0;
        }
    }
    if (!text && !voice && !wav) {
        usage();
        return 0;
    }
    paths_init();
    log_init(g_paths.log_file);
    log_msg("Sokari %s arrancando (Linux, modo %s).", SOKARI_VERSION, text ? "texto" : wav ? "simulación" : "voz");
    config_load();
    memory_init();
    http_init();
    int rc = text ? run_text() : run_voice(wav);
    log_msg("Sokari cerrado.");
    return rc;
}
