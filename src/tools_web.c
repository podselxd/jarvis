#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app.h"
#include "http.h"
#include "log.h"
#include "sounds.h"
#include "tools.h"
#include "util.h"

#define MAX_RESULTS 4
#define MAX_PAGE_READ_CHARS 6000

static void append_codepoint(StrBuf *sb, unsigned long cp)
{
    char b[4];
    if (cp < 0x80) {
        sb_append_char(sb, (char)cp);
    } else if (cp < 0x800) {
        b[0] = (char)(0xC0 | (cp >> 6));
        b[1] = (char)(0x80 | (cp & 0x3F));
        sb_append_n(sb, b, 2);
    } else if (cp < 0x10000) {
        b[0] = (char)(0xE0 | (cp >> 12));
        b[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[2] = (char)(0x80 | (cp & 0x3F));
        sb_append_n(sb, b, 3);
    } else if (cp < 0x110000) {
        b[0] = (char)(0xF0 | (cp >> 18));
        b[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        b[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        b[3] = (char)(0x80 | (cp & 0x3F));
        sb_append_n(sb, b, 4);
    }
}

static const struct {
    const char *name;
    unsigned cp;
} ENTITIES[] = {
    {"amp", '&'},       {"lt", '<'},        {"gt", '>'},        {"quot", '"'},      {"apos", '\''},
    {"nbsp", ' '},      {"aacute", 0xE1},   {"eacute", 0xE9},   {"iacute", 0xED},   {"oacute", 0xF3},
    {"uacute", 0xFA},   {"Aacute", 0xC1},   {"Eacute", 0xC9},   {"Iacute", 0xCD},   {"Oacute", 0xD3},
    {"Uacute", 0xDA},   {"ntilde", 0xF1},   {"Ntilde", 0xD1},   {"uuml", 0xFC},     {"Uuml", 0xDC},
    {"iexcl", 0xA1},    {"iquest", 0xBF},   {"laquo", 0xAB},    {"raquo", 0xBB},    {"mdash", 0x2014},
    {"ndash", 0x2013},  {"hellip", 0x2026}, {"copy", 0xA9},     {"reg", 0xAE},      {"euro", 0x20AC},
    {"lsquo", 0x2018},  {"rsquo", 0x2019},  {"ldquo", 0x201C},  {"rdquo", 0x201D},  {"deg", 0xB0},
    {"middot", 0xB7},   {"bull", 0x2022},   {"times", 0xD7},    {"ccedil", 0xE7},   {"agrave", 0xE0},
    {"egrave", 0xE8},   {"ouml", 0xF6},     {"auml", 0xE4},     {"szlig", 0xDF},    {"trade", 0x2122},
};

/* Decodifica &algo; en p (que apunta a '&'). Devuelve cuántos bytes consumió. */
static size_t decode_entity(const char *p, StrBuf *sb)
{
    const char *semi = strchr(p, ';');
    if (!semi || semi - p > 12) {
        sb_append_char(sb, '&');
        return 1;
    }
    size_t len = (size_t)(semi - p - 1);
    if (p[1] == '#') {
        unsigned long cp = (p[2] == 'x' || p[2] == 'X') ? strtoul(p + 3, NULL, 16) : strtoul(p + 2, NULL, 10);
        append_codepoint(sb, cp == 0xA0 ? ' ' : cp);
        return (size_t)(semi - p + 1);
    }
    for (size_t i = 0; i < sizeof ENTITIES / sizeof *ENTITIES; i++) {
        if (strlen(ENTITIES[i].name) == len && !strncmp(p + 1, ENTITIES[i].name, len)) {
            append_codepoint(sb, ENTITIES[i].cp);
            return (size_t)(semi - p + 1);
        }
    }
    sb_append_char(sb, '&');
    return 1;
}

static bool tag_is(const char *p, const char *name)
{
    size_t n = strlen(name);
    return !_strnicmp(p, name, n) && !isalnum((unsigned char)p[n]);
}

static const char *SKIP_ELEMENTS[] = {"script", "style", "nav", "footer", "header", "aside", "noscript", "svg", "template"};

/* HTML -> texto plano: descarta scripts/estilos/menús y todas las etiquetas,
   decodifica entidades y colapsa espacios. Sin DOM: un recorrido lineal. */
static char *html_to_text(const char *html)
{
    StrBuf sb;
    sb_init(&sb);
    const char *p = html;
    while (*p) {
        if (*p == '<') {
            if (!strncmp(p, "<!--", 4)) {
                const char *end = strstr(p + 4, "-->");
                p = end ? end + 3 : p + strlen(p);
                continue;
            }
            const char *name = p + 1;
            bool skipped = false;
            for (size_t i = 0; i < sizeof SKIP_ELEMENTS / sizeof *SKIP_ELEMENTS && !skipped; i++) {
                if (!tag_is(name, SKIP_ELEMENTS[i])) continue;
                char close[32];
                snprintf(close, sizeof close, "</%s", SKIP_ELEMENTS[i]);
                const char *q = p + 1;
                const char *found = NULL;
                size_t cl = strlen(close);
                for (; *q; q++)
                    if (*q == '<' && !_strnicmp(q, close, cl)) {
                        found = q;
                        break;
                    }
                if (found) {
                    const char *gt = strchr(found, '>');
                    p = gt ? gt + 1 : found + strlen(found);
                } else {
                    p += strlen(p);
                }
                skipped = true;
            }
            if (skipped) {
                sb_append_char(&sb, ' ');
                continue;
            }
            char quote = 0;
            p++;
            while (*p && (quote || *p != '>')) {
                if (quote && *p == quote) quote = 0;
                else if (!quote && (*p == '"' || *p == '\'')) quote = *p;
                p++;
            }
            if (*p) p++;
            sb_append_char(&sb, ' ');
        } else if (*p == '&') {
            p += decode_entity(p, &sb);
        } else {
            sb_append_char(&sb, *p++);
        }
    }
    char *r = sb_steal(&sb);
    str_collapse_spaces(r);
    return r;
}

static char *url_decode(const char *s, size_t n)
{
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '%' && i + 2 < n && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2])) {
            char hex[3] = {s[i + 1], s[i + 2], 0};
            sb_append_char(&sb, (char)strtol(hex, NULL, 16));
            i += 2;
        } else if (s[i] == '+') {
            sb_append_char(&sb, ' ');
        } else {
            sb_append_char(&sb, s[i]);
        }
    }
    return sb_steal(&sb);
}

static char *inner_text(const char *start, const char *end_tag)
{
    const char *gt = strchr(start, '>');
    if (!gt) return xstrdup("");
    const char *end = strstr(gt + 1, end_tag);
    if (!end) return xstrdup("");
    char *raw = xstrndup(gt + 1, (size_t)(end - gt - 1));
    char *t = html_to_text(raw);
    free(raw);
    return t;
}

static int search_ddg(const char *query, StrBuf *out)
{
    char *q = url_encode(query);
    char *url = str_printf("https://html.duckduckgo.com/html/?q=%s&kl=mx-es", q);
    free(q);
    HttpResponse r = http_get(url, 15000, true);
    free(url);
    int found = 0;
    if (r.status == 200) {
        const char *p = r.body;
        while (found < MAX_RESULTS && (p = strstr(p, "class=\"result__a\""))) {
            const char *tag_start = p;
            while (tag_start > r.body && *tag_start != '<') tag_start--;
            const char *href = strstr(tag_start, "href=\"");
            const char *gt = strchr(p, '>');
            p++;
            if (!href || !gt || href > gt) continue;
            href += 6;
            const char *href_end = strchr(href, '"');
            if (!href_end) continue;
            char *link = NULL;
            const char *uddg = strstr(href, "uddg=");
            if (uddg && uddg < href_end) {
                uddg += 5;
                const char *amp = uddg;
                while (amp < href_end && *amp != '&') amp++;
                link = url_decode(uddg, (size_t)(amp - uddg));
            } else if (str_starts_with(href, "http")) {
                link = xstrndup(href, (size_t)(href_end - href));
            }
            if (!link || strstr(link, "duckduckgo.com/y.js")) {
                free(link);
                continue;
            }
            char *title = inner_text(p - 1, "</a>");
            const char *snip = strstr(p, "class=\"result__snippet\"");
            const char *next = strstr(p, "class=\"result__a\"");
            char *body = (snip && (!next || snip < next)) ? inner_text(snip, "</a>") : xstrdup("");
            sb_appendf(out, "%s%s (%s): %s", out->len ? "\n" : "", title, link, body);
            free(title);
            free(body);
            free(link);
            found++;
        }
    }
    http_response_free(&r);
    return found;
}

static char *xml_field(const char *item, const char *item_end, const char *tag)
{
    char open[32], close[32];
    snprintf(open, sizeof open, "<%s>", tag);
    snprintf(close, sizeof close, "</%s>", tag);
    const char *s = strstr(item, open);
    if (!s || s > item_end) return xstrdup("");
    s += strlen(open);
    const char *e = strstr(s, close);
    if (!e || e > item_end) return xstrdup("");
    char *raw = xstrndup(s, (size_t)(e - s));
    char *t = html_to_text(raw);
    free(raw);
    return t;
}

static int search_bing(const char *query, StrBuf *out)
{
    char *q = url_encode(query);
    char *url = str_printf("https://www.bing.com/search?q=%s&format=rss&setlang=es&cc=MX", q);
    free(q);
    HttpResponse r = http_get(url, 15000, true);
    free(url);
    int found = 0;
    if (r.status == 200) {
        const char *p = r.body;
        while (found < MAX_RESULTS && (p = strstr(p, "<item>"))) {
            const char *end = strstr(p, "</item>");
            if (!end) break;
            char *title = xml_field(p, end, "title");
            char *link = xml_field(p, end, "link");
            char *desc = xml_field(p, end, "description");
            sb_appendf(out, "%s%s (%s): %s", out->len ? "\n" : "", title, link, desc);
            free(title);
            free(link);
            free(desc);
            found++;
            p = end;
        }
    }
    http_response_free(&r);
    return found;
}

char *tool_web_search(const cJSON *a)
{
    char *query = str_trim(arg_str(a, "query"));
    if (!*query) {
        free(query);
        return xstrdup("No me dijiste qué buscar.");
    }
    app_status("Buscando en internet…");
    sound_search_start();
    StrBuf out;
    sb_init(&out);
    int n = search_ddg(query, &out);
    if (!n) n = search_bing(query, &out);
    sound_search_stop();
    app_status("");
    free(query);
    if (!n) {
        sb_free(&out);
        return xstrdup("No encontré resultados (o no hay conexión a internet).");
    }
    return sb_steal(&out);
}

/* Defensa extra: una página que el modelo quiera abrir nunca puede apuntar a
   tu red local (router, Tailscale, localhost). */
static bool host_is_private(const char *url)
{
    const char *h = strstr(url, "://");
    h = h ? h + 3 : url;
    char host[256];
    size_t n = 0;
    while (*h && *h != '/' && *h != ':' && *h != '?' && *h != '#' && n < sizeof host - 1) host[n++] = (char)tolower((unsigned char)*h++);
    host[n] = 0;
    if (!strcmp(host, "localhost") || str_ends_with(host, ".local") || str_ends_with(host, ".internal") ||
        str_ends_with(host, ".ts.net"))
        return true;
    unsigned a, b, c, d;
    if (sscanf(host, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
        if (a == 10 || a == 127 || a == 0 || (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) ||
            (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127))
            return true;
    }
    return host[0] == '[';
}

static bool is_utf8_content(const HttpResponse *r)
{
    if (r->content_type && str_contains_ci(r->content_type, "charset=")) return str_contains_ci(r->content_type, "utf-8");
    const char *meta = r->body;
    size_t scan = r->body_len < 4096 ? r->body_len : 4096;
    char *head = xstrndup(meta, scan);
    char *low = str_lower(head);
    bool latin = strstr(low, "charset=iso-8859") || strstr(low, "charset=\"iso-8859") ||
                 strstr(low, "charset=windows-1252") || strstr(low, "charset=\"windows-1252");
    free(low);
    free(head);
    return !latin;
}

char *tool_leer_pagina(const cJSON *a)
{
    char *url = str_trim(arg_str(a, "url"));
    if (!*url) {
        free(url);
        return xstrdup("No me diste ninguna URL.");
    }
    if (!str_starts_with(url, "http://") && !str_starts_with(url, "https://")) {
        if (strstr(url, "://")) {
            free(url);
            return xstrdup("Solo puedo abrir páginas web normales (http o https).");
        }
        char *u = str_printf("https://%s", url);
        free(url);
        url = u;
    }
    if (host_is_private(url)) {
        free(url);
        return xstrdup("Por seguridad no leo direcciones de tu red local.");
    }
    HttpRequest req = {.method = "GET", .url = url, .timeout_ms = 15000, .browser_ua = true, .max_bytes = 3u << 20};
    HttpResponse r = http_request(&req);
    char *result;
    if (r.status != 200) {
        result = r.error ? str_printf("No pude abrir esa página: %s", r.error)
                         : str_printf("No pude abrir esa página (respondió %d).", r.status);
    } else {
        char *html = r.body;
        char *converted = NULL;
        if (!is_utf8_content(&r)) {
            int wn = MultiByteToWideChar(1252, 0, r.body, (int)r.body_len, NULL, 0);
            wchar_t *w = xmalloc(sizeof(wchar_t) * (size_t)(wn + 1));
            MultiByteToWideChar(1252, 0, r.body, (int)r.body_len, w, wn);
            converted = wide_n_to_utf8(w, wn);
            free(w);
            html = converted;
        }
        char *text = html_to_text(html);
        free(converted);
        if (str_is_blank(text)) {
            free(text);
            result = xstrdup("Entré a la página pero no encontré texto legible ahí.");
        } else {
            size_t chars = 0, i = 0;
            while (text[i] && chars < MAX_PAGE_READ_CHARS) {
                i++;
                while (((unsigned char)text[i] & 0xC0) == 0x80) i++;
                chars++;
            }
            if (text[i]) {
                text[i] = 0;
                result = str_printf("%s [...se cortó aquí, la página sigue...]", text);
                free(text);
            } else {
                result = text;
            }
        }
    }
    http_response_free(&r);
    free(url);
    return result;
}
