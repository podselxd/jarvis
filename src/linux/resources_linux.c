/* resources.h en Linux: los archivos que recursos.S metió al ejecutable. */
#include <stdlib.h>

#include "resource.h"
#include "resources.h"
#include "util.h"

extern const char sokari_res_tools[], sokari_res_tools_end[];
extern const char sokari_res_prompt[], sokari_res_prompt_end[];
extern const char sokari_res_wakeword[], sokari_res_wakeword_end[];
extern const char sokari_res_hey[], sokari_res_hey_end[];

const void *res_data(int id, size_t *len)
{
    const char *start, *end;
    switch (id) {
    case IDR_TOOLS_JSON: start = sokari_res_tools, end = sokari_res_tools_end; break;
    case IDR_SYSTEM_PROMPT: start = sokari_res_prompt, end = sokari_res_prompt_end; break;
    case IDR_WAKEWORD: start = sokari_res_wakeword, end = sokari_res_wakeword_end; break;
    case IDR_HEY_SOKARI: start = sokari_res_hey, end = sokari_res_hey_end; break;
    default: return NULL;
    }
    if (end == start) return NULL;
    if (len) *len = (size_t)(end - start);
    return start;
}

char *res_string(int id)
{
    size_t n = 0;
    const char *p = res_data(id, &n);
    if (!p) return xstrdup("");
    return xstrndup(p, n);
}

bool res_has_wake_word(void)
{
    size_t n = 0;
    return res_data(IDR_HEY_SOKARI, &n) && n > 8;
}
