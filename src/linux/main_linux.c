/* Sokari en Linux. En esta parte corre en modo texto: escribes lo que le
   dirías y te contesta en la terminal, con el mismo agente, las mismas
   herramientas y las mismas confirmaciones que en Windows. La voz y la
   interfaz llegan en las siguientes partes. */
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent.h"
#include "config.h"
#include "http.h"
#include "log.h"
#include "memory.h"
#include "util.h"

bool app_quit_requested(void);

static void usage(void)
{
    printf("Sokari %s para Linux\n\n"
           "  sokari --texto     platicar escribiendo (Ctrl+D para salir)\n"
           "  sokari --version   la versión\n\n"
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

int main(int argc, char **argv)
{
    bool text = false;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--texto")) {
            text = true;
        } else if (!strcmp(argv[i], "--version")) {
            printf("%s\n", SOKARI_VERSION);
            return 0;
        } else {
            usage();
            return strcmp(argv[i], "--ayuda") && strcmp(argv[i], "--help") ? 1 : 0;
        }
    }
    if (!text) {
        usage();
        return 0;
    }
    paths_init();
    log_init(g_paths.log_file);
    log_msg("Sokari %s arrancando (Linux, modo texto).", SOKARI_VERSION);
    config_load();
    memory_init();
    http_init();
    int rc = run_text();
    log_msg("Sokari cerrado.");
    return rc;
}
