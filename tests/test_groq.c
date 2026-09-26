#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "groq.h"
#include "http.h"
#include "log.h"
#include "resource.h"
#include "resources.h"
#include "util.h"

#ifdef _WIN32
int wmain(int argc, wchar_t **argv)
#else
int main(int argc, char **argv)
#endif
{
    SetConsoleOutputCP(CP_UTF8);
    log_to_console(1);
    paths_init();
    config_load();
    http_init();

    char *key = config_api_key();
    if (!*key) {
        fprintf(stderr, "Sin API key en %ls\n", g_paths.config_file);
        return 1;
    }
    free(key);

    cJSON *tools = cJSON_Parse(res_string(IDR_TOOLS_JSON));
    printf("tools cargadas: %d\n", tools ? cJSON_GetArraySize(tools) : -1);

    cJSON *msgs = cJSON_CreateArray();
    cJSON *sys = cJSON_CreateObject();
    cJSON_AddStringToObject(sys, "role", "system");
    cJSON_AddStringToObject(sys, "content", res_string(IDR_SYSTEM_PROMPT));
    cJSON_AddItemToArray(msgs, sys);
    cJSON *user = cJSON_CreateObject();
    cJSON_AddStringToObject(user, "role", "user");
#ifdef _WIN32
    char *q = argc > 1 ? wide_to_utf8(argv[1]) : xstrdup("Hola Sokari, ¿cuánto es 17 por 23?");
#else
    char *q = xstrdup(argc > 1 ? argv[1] : "Hola Sokari, ¿cuánto es 17 por 23?");
#endif
    cJSON_AddStringToObject(user, "content", q);
    cJSON_AddItemToArray(msgs, user);

    GroqError err = {0};
    uint64_t t0 = now_ms();
    cJSON *reply = groq_chat(msgs, tools, &err);
    printf("tardó %llu ms\n", (unsigned long long)(now_ms() - t0));
    if (!reply) {
        printf("ERROR status=%d http=%d retry=%d detalle=%s\n", err.status, err.http_status, err.retry_after,
               err.detail ? err.detail : "");
        return 2;
    }
    char *s = cJSON_Print(reply);
    printf("%s\n", s);
    return 0;
}
