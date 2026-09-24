/* Verifica que los recursos metidos en el exe (herramientas, prompt y modelo
   de "Hey Jarvis") se lean bien. No usa internet ni abre ventanas. */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "resource.h"
#include "resources.h"
#include "third_party/cJSON.h"

int wmain(void)
{
    char *tools = res_string(IDR_TOOLS_JSON);
    char *prompt = res_string(IDR_SYSTEM_PROMPT);
    size_t ww_len = 0;
    const char *ww = res_data(IDR_WAKEWORD, &ww_len);

    cJSON *t = cJSON_Parse(tools);
    if (!cJSON_IsArray(t)) {
        printf("FALLA: tools.json no es un arreglo JSON válido\n");
        return 1;
    }
    int n = cJSON_GetArraySize(t), bad = 0;
    cJSON *tool;
    cJSON_ArrayForEach(tool, t)
    {
        cJSON *fn = cJSON_GetObjectItem(tool, "function");
        if (!cJSON_IsString(cJSON_GetObjectItem(fn, "name")) || !cJSON_IsObject(cJSON_GetObjectItem(fn, "parameters")))
            bad++;
    }
    char *compact = cJSON_PrintUnformatted(t);
    printf("herramientas: %d (%d mal formadas) | a Groq se mandan %zu bytes\n", n, bad, strlen(compact));
    printf("prompt: %zu bytes | modelo de wake word: %zu bytes (%.4s)\n", strlen(prompt), ww_len, ww ? ww : "----");
    bool ok = n > 0 && !bad && *prompt && ww && ww_len > 4 && !memcmp(ww, "JWW1", 4);
    printf(ok ? "ok\n" : "FALLA\n");
    cJSON_free(compact);
    cJSON_Delete(t);
    free(tools);
    free(prompt);
    return ok ? 0 : 1;
}
