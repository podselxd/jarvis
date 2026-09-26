/* Lo de la voz que no depende del sistema. */
#include <stdlib.h>

#include "tts.h"
#include "util.h"

/* El modelo tiene prohibido usar markdown, pero por las dudas: que la voz no
   lea asteriscos, numerales ni links enteros. */
char *tts_clean_text(const char *text)
{
    StrBuf sb;
    sb_init(&sb);
    for (const char *p = text; *p; p++) {
        if (str_starts_with(p, "http://") || str_starts_with(p, "https://")) {
            sb_append(&sb, "el enlace");
            while (*p && *p != ' ' && *p != '\n' && *p != ')') p++;
            if (!*p) break;
        }
        char c = *p;
        if (c == '*' || c == '#' || c == '`' || c == '_' || c == '~' || c == '|' || c == '>') {
            sb_append_char(&sb, ' ');
            continue;
        }
        sb_append_char(&sb, c);
    }
    char *s = sb_steal(&sb);
    str_collapse_spaces(s);
    return s;
}
