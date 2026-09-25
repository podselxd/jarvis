/* Lo del cliente HTTP que no depende del sistema. */
#include <stdlib.h>

#include "http.h"
#include "util.h"

char *url_encode(const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    StrBuf sb;
    sb_init(&sb);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '-' ||
            *p == '_' || *p == '.' || *p == '~') {
            sb_append_char(&sb, (char)*p);
        } else {
            char e[3] = {'%', hex[*p >> 4], hex[*p & 15]};
            sb_append_n(&sb, e, 3);
        }
    }
    return sb_steal(&sb);
}
