/* Lo que no depende del sistema: memoria, textos, fechas y comparaciones.
   Archivos, rutas, relojes y conversiones de texto ancho están en util_win.c
   (y en src/linux/util_linux.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "util.h"

static void oom(void)
{
    fatal_error("Sokari se quedó sin memoria.", 3);
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) oom();
    return p;
}

void *xcalloc(size_t count, size_t size)
{
    void *p = calloc(count ? count : 1, size ? size : 1);
    if (!p) oom();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) oom();
    return q;
}

char *xstrdup(const char *s)
{
    if (!s) s = "";
    size_t n = strlen(s);
    char *d = xmalloc(n + 1);
    memcpy(d, s, n + 1);
    return d;
}

char *xstrndup(const char *s, size_t n)
{
    char *d = xmalloc(n + 1);
    memcpy(d, s, n);
    d[n] = 0;
    return d;
}

wchar_t *xwcsdup(const wchar_t *s)
{
    if (!s) s = L"";
    size_t n = wcslen(s);
    wchar_t *d = xmalloc((n + 1) * sizeof(wchar_t));
    memcpy(d, s, (n + 1) * sizeof(wchar_t));
    return d;
}

void sb_init(StrBuf *sb)
{
    sb->cap = 64;
    sb->len = 0;
    sb->data = xmalloc(sb->cap);
    sb->data[0] = 0;
}

void sb_free(StrBuf *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->len = sb->cap = 0;
}

void sb_reserve(StrBuf *sb, size_t extra)
{
    if (sb->len + extra + 1 <= sb->cap) return;
    size_t cap = sb->cap ? sb->cap : 64;
    while (cap < sb->len + extra + 1) cap *= 2;
    sb->data = xrealloc(sb->data, cap);
    sb->cap = cap;
}

void sb_append_n(StrBuf *sb, const char *s, size_t n)
{
    sb_reserve(sb, n);
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = 0;
}

void sb_append(StrBuf *sb, const char *s)
{
    if (s) sb_append_n(sb, s, strlen(s));
}

void sb_append_char(StrBuf *sb, char c)
{
    sb_append_n(sb, &c, 1);
}

void sb_vappendf(StrBuf *sb, const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n <= 0) return;
    sb_reserve(sb, (size_t)n);
    vsnprintf(sb->data + sb->len, (size_t)n + 1, fmt, ap);
    sb->len += (size_t)n;
}

void sb_appendf(StrBuf *sb, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    sb_vappendf(sb, fmt, ap);
    va_end(ap);
}

char *sb_steal(StrBuf *sb)
{
    char *d = sb->data;
    sb->data = NULL;
    sb->len = sb->cap = 0;
    return d;
}

char *str_printf(const char *fmt, ...)
{
    StrBuf sb;
    sb_init(&sb);
    va_list ap;
    va_start(ap, fmt);
    sb_vappendf(&sb, fmt, ap);
    va_end(ap);
    return sb_steal(&sb);
}





bool str_contains_ci(const char *haystack, const char *needle)
{
    if (!haystack || !needle) return false;
    if (!*needle) return true;
    char *h = str_lower(haystack), *n = str_lower(needle);
    bool found = strstr(h, n) != NULL;
    free(h);
    free(n);
    return found;
}

bool str_eq_ci(const char *a, const char *b)
{
    if (!a || !b) return a == b;
    char *x = str_lower(a), *y = str_lower(b);
    bool eq = strcmp(x, y) == 0;
    free(x);
    free(y);
    return eq;
}

bool str_starts_with(const char *s, const char *prefix)
{
    return s && prefix && strncmp(s, prefix, strlen(prefix)) == 0;
}

bool str_ends_with(const char *s, const char *suffix)
{
    if (!s || !suffix) return false;
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && memcmp(s + a - b, suffix, b) == 0;
}

static bool is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

char *str_trim(const char *s)
{
    if (!s) return xstrdup("");
    while (*s && is_space((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && is_space((unsigned char)s[n - 1])) n--;
    return xstrndup(s, n);
}

bool str_is_blank(const char *s)
{
    if (!s) return true;
    for (; *s; s++)
        if (!is_space((unsigned char)*s)) return false;
    return true;
}

void str_collapse_spaces(char *s)
{
    char *w = s;
    bool pending = false;
    for (char *r = s; *r; r++) {
        if (is_space((unsigned char)*r)) {
            pending = w != s;
            continue;
        }
        if (pending) *w++ = ' ';
        pending = false;
        *w++ = *r;
    }
    *w = 0;
}

size_t utf8_truncate_len(const char *s, size_t max_bytes)
{
    size_t n = strlen(s);
    if (n <= max_bytes) return n;
    size_t cut = max_bytes;
    while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
    return cut;
}









wchar_t *path_dirname(const wchar_t *p)
{
    const wchar_t *slash = wcsrchr(p, L'\\');
    const wchar_t *fwd = wcsrchr(p, L'/');
    if (fwd > slash) slash = fwd;
    if (!slash) return xwcsdup(L".");
    size_t n = (size_t)(slash - p);
    wchar_t *r = xmalloc((n + 1) * sizeof(wchar_t));
    memcpy(r, p, n * sizeof(wchar_t));
    r[n] = 0;
    return r;
}

const wchar_t *path_basename(const wchar_t *p)
{
    const wchar_t *slash = wcsrchr(p, L'\\');
    const wchar_t *fwd = wcsrchr(p, L'/');
    if (fwd > slash) slash = fwd;
    return slash ? slash + 1 : p;
}







char *local_iso_now(void)
{
    return format_epoch_local(now_epoch(), "%Y-%m-%dT%H:%M:%S");
}

static bool read_int(const char **p, int digits, int *out)
{
    int v = 0;
    for (int i = 0; i < digits; i++) {
        if ((*p)[i] < '0' || (*p)[i] > '9') return false;
        v = v * 10 + ((*p)[i] - '0');
    }
    *p += digits;
    *out = v;
    return true;
}

/* Acepta lo mismo que datetime.fromisoformat de Python usaba en la versión
   anterior: fecha sola, fecha+hora con 'T' o espacio, segundos/fracción
   opcionales, y zona horaria opcional (Z o ±HH:MM). Sin zona = hora local. */
bool parse_iso_local(const char *s, double *out_epoch)
{
    if (!s) return false;
    while (*s == ' ') s++;
    int Y, M, D, h = 0, m = 0, sec = 0;
    const char *p = s;
    if (!read_int(&p, 4, &Y) || *p++ != '-' || !read_int(&p, 2, &M) || *p++ != '-' || !read_int(&p, 2, &D))
        return false;
    bool has_tz = false;
    int tz_minutes = 0;
    if (*p == 'T' || *p == 't' || *p == ' ') {
        p++;
        if (!read_int(&p, 2, &h) || *p++ != ':' || !read_int(&p, 2, &m)) return false;
        if (*p == ':') {
            p++;
            if (!read_int(&p, 2, &sec)) return false;
            if (*p == '.' || *p == ',') {
                p++;
                while (*p >= '0' && *p <= '9') p++;
            }
        }
        if (*p == 'Z' || *p == 'z') {
            has_tz = true;
            p++;
        } else if (*p == '+' || *p == '-') {
            int sign = *p == '-' ? -1 : 1, th, tm = 0;
            p++;
            if (!read_int(&p, 2, &th)) return false;
            if (*p == ':') p++;
            if (*p >= '0' && *p <= '9' && !read_int(&p, 2, &tm)) return false;
            has_tz = true;
            tz_minutes = sign * (th * 60 + tm);
        }
    }
    while (*p == ' ') p++;
    if (*p) return false;
    if (M < 1 || M > 12 || D < 1 || D > 31 || h > 23 || m > 59 || sec > 60) return false;

    if (!civil_to_epoch(Y, M, D, h, m, sec > 59 ? 59 : sec, has_tz, out_epoch)) return false;
    if (has_tz) *out_epoch -= tz_minutes * 60.0;
    return true;
}

bool secure_equal(const char *a, const char *b)
{
    if (!a || !b) return false;
    size_t na = strlen(a), nb = strlen(b);
    size_t n = na > nb ? na : nb;
    unsigned char diff = (unsigned char)(na != nb);
    for (size_t i = 0; i < n; i++) {
        unsigned char x = i < na ? (unsigned char)a[i] : 0;
        unsigned char y = i < nb ? (unsigned char)b[i] : 0;
        diff |= (unsigned char)(x ^ y);
    }
    return diff == 0;
}


char *hex_encode(const unsigned char *data, size_t n)
{
    static const char digits[] = "0123456789abcdef";
    char *r = xmalloc(n * 2 + 1);
    for (size_t i = 0; i < n; i++) {
        r[i * 2] = digits[data[i] >> 4];
        r[i * 2 + 1] = digits[data[i] & 15];
    }
    r[n * 2] = 0;
    return r;
}

