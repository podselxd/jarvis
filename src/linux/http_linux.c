/* http.h con libcurl. Se comporta como la versión de WinHTTP: los mismos
   límites de tiempo (sin datos durante timeout_ms, no un tope total), el
   mismo tope de bytes, las mismas cabeceras de cupo de Groq y los mismos
   mensajes de error. */
#include <windows.h>

#include <ctype.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "http.h"
#include "log.h"
#include "util.h"

#define DEFAULT_MAX_BYTES (32u * 1024u * 1024u)

static const char *API_UA = "Sokari/" SOKARI_VERSION " (Linux)";
static const char *WEB_UA =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/128.0.0.0 Safari/537.36";

static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static bool g_ok;

static void init_once(void)
{
    g_ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
}

bool http_init(void)
{
    pthread_once(&g_once, init_once);
    return g_ok;
}

void http_response_free(HttpResponse *r)
{
    free(r->body);
    free(r->content_type);
    free(r->error);
    free(r->location);
    memset(r, 0, sizeof *r);
}

/* Los mismos textos que con WinHTTP, y error_code con el número de WinHTTP
   equivalente (la malla distingue "no se pudo conectar"). */
static char *curl_error_text(CURLcode c, unsigned long *code)
{
    switch (c) {
    case CURLE_OPERATION_TIMEDOUT: *code = 12002; return xstrdup("se agotó el tiempo de espera");
    case CURLE_COULDNT_RESOLVE_HOST:
    case CURLE_COULDNT_RESOLVE_PROXY: *code = 12007; return xstrdup("no se pudo resolver el nombre (¿sin internet?)");
    case CURLE_COULDNT_CONNECT: *code = 12029; return xstrdup("no se pudo conectar");
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_PARTIAL_FILE: *code = 12030; return xstrdup("se cortó la conexión");
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE: *code = 12175; return xstrdup("falló la conexión segura (TLS)");
    case CURLE_URL_MALFORMAT:
    case CURLE_UNSUPPORTED_PROTOCOL: *code = 12005; return xstrdup("la dirección no es válida");
    default: *code = (unsigned long)c; return str_printf("error de red %d", (int)c);
    }
}

/* Duraciones estilo Go que manda Groq: "7.66s", "1m2.5s", "350ms". */
static double parse_duration(const char *s)
{
    double total = 0;
    bool any = false;
    while (*s) {
        double num = 0, frac = 0, scale = 1;
        bool dot = false;
        while ((*s >= '0' && *s <= '9') || *s == '.') {
            if (*s == '.') dot = true;
            else if (dot) frac += (*s - '0') * (scale /= 10);
            else num = num * 10 + (*s - '0');
            s++;
            any = true;
        }
        num += frac;
        if (s[0] == 'm' && s[1] == 's') total += num / 1000, s += 2;
        else if (*s == 'h') total += num * 3600, s++;
        else if (*s == 'm') total += num * 60, s++;
        else if (*s == 's') total += num, s++;
        else if (*s) s++;
    }
    return any ? total : -1;
}

typedef struct {
    const HttpRequest *req;
    HttpResponse *resp;
    CURL *curl;
    StrBuf body;
    size_t cap;
    FILE *out;
    bool out_failed, capped;
} Transfer;

/* El valor de "Nombre: valor" si la línea es esa cabecera (sin mayúsculas). */
static char *header_value(const char *line, size_t n, const char *name)
{
    size_t k = strlen(name);
    if (n <= k || strncasecmp(line, name, k) || line[k] != ':') return NULL;
    const char *v = line + k + 1;
    const char *end = line + n;
    while (v < end && (*v == ' ' || *v == '\t')) v++;
    while (end > v && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ')) end--;
    return xstrndup(v, (size_t)(end - v));
}

static size_t on_header(char *line, size_t size, size_t nitems, void *userdata)
{
    Transfer *t = userdata;
    size_t n = size * nitems;
    HttpResponse *r = t->resp;
    /* Siguiendo redirecciones llegan las cabeceras de cada respuesta: cuentan
       las de la última. */
    if (n >= 5 && !strncmp(line, "HTTP/", 5)) {
        free(r->content_type);
        r->content_type = NULL;
        free(r->location);
        r->location = NULL;
        r->retry_after = r->rl_remaining_tokens = -1;
        r->rl_reset_tokens = -1;
        return n;
    }
    char *v;
    if ((v = header_value(line, n, "Content-Type"))) {
        free(r->content_type);
        r->content_type = v;
    } else if ((v = header_value(line, n, "Location"))) {
        free(r->location);
        r->location = t->req->no_redirects && strlen(v) < 64 * 1024 ? v : NULL;
        if (!r->location) free(v);
    } else if ((v = header_value(line, n, "Retry-After"))) {
        double s = atof(v);
        r->retry_after = s > 0 ? (int)(s + 0.999) : -1;
        free(v);
    } else if ((v = header_value(line, n, "x-ratelimit-remaining-tokens"))) {
        r->rl_remaining_tokens = atoi(v);
        free(v);
    } else if ((v = header_value(line, n, "x-ratelimit-reset-tokens"))) {
        r->rl_reset_tokens = parse_duration(v);
        free(v);
    }
    return n;
}

static size_t on_body(char *data, size_t size, size_t nmemb, void *userdata)
{
    Transfer *t = userdata;
    size_t n = size * nmemb;
    if (t->req->download_to && !t->out && !t->out_failed) {
        long status = 0;
        curl_easy_getinfo(t->curl, CURLINFO_RESPONSE_CODE, &status);
        if (status == 200) {
            char *p = wide_to_utf8(t->req->download_to);
            t->out = fopen(p, "wb");
            free(p);
            if (!t->out) {
                t->out_failed = true;
                return 0;
            }
        }
    }
    if (t->out) {
        if (fwrite(data, 1, n, t->out) != n) {
            t->out_failed = true;
            return 0;
        }
        return n;
    }
    /* Pasado el tope se deja de leer, como en Windows: lo que llegó se queda. */
    size_t keep = t->body.len + n > t->cap ? t->cap - t->body.len : n;
    sb_append_n(&t->body, data, keep);
    if (keep < n) {
        t->capped = true;
        return 0;
    }
    return n;
}

HttpResponse http_request(const HttpRequest *r)
{
    HttpResponse resp = {0};
    resp.retry_after = -1;
    resp.rl_remaining_tokens = -1;
    resp.rl_reset_tokens = -1;
    Transfer t = {.req = r, .resp = &resp, .cap = r->max_bytes ? r->max_bytes : DEFAULT_MAX_BYTES};
    sb_init(&t.body);
    CURL *c = http_init() ? curl_easy_init() : NULL;
    if (!c) {
        resp.error = xstrdup("libcurl no está disponible");
        resp.body = xstrdup("");
        return resp;
    }
    t.curl = c;
    struct curl_slist *hdrs = NULL;
    if (r->headers) {
        /* "Clave: valor\r\n"… como en WinHTTP. */
        const char *p = r->headers;
        while (*p) {
            const char *e = strstr(p, "\r\n");
            size_t n = e ? (size_t)(e - p) : strlen(p);
            if (n) {
                char *line = xstrndup(p, n);
                hdrs = curl_slist_append(hdrs, line);
                free(line);
            }
            p += n + (e ? 2 : 0);
        }
    }
    /* Sin "Expect: 100-continue": Groq contesta directo. */
    hdrs = curl_slist_append(hdrs, "Expect:");
    const char *method = r->method ? r->method : "GET";
    curl_easy_setopt(c, CURLOPT_URL, r->url);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, r->no_redirects ? 0L : 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, r->browser_ua ? WEB_UA : API_UA);
    curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(c, CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
    const char *ca = getenv("SSL_CERT_FILE");
    if (ca && *ca) curl_easy_setopt(c, CURLOPT_CAINFO, ca);
    int to = r->timeout_ms > 0 ? r->timeout_ms : 60000;
    int cto = r->connect_timeout_ms > 0 ? r->connect_timeout_ms : to;
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, (long)cto);
    /* Como WinHTTP: se corta si pasan timeout_ms sin que llegue nada. */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, (long)((to + 999) / 1000));
    if (!strcmp(method, "GET")) {
        curl_easy_setopt(c, CURLOPT_HTTPGET, 1L);
    } else if (!strcmp(method, "POST")) {
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, r->body ? r->body : "");
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)r->body_len);
    } else {
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method);
        if (r->body_len) {
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, r->body);
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)r->body_len);
        }
    }
    curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, on_header);
    curl_easy_setopt(c, CURLOPT_HEADERDATA, &t);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_body);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);

    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (rc == CURLE_WRITE_ERROR && t.capped) rc = CURLE_OK; /* llegó al tope de bytes: no es error */
    if (t.out_failed) {
        resp.error = xstrdup(t.out ? "no pude escribir la descarga" : "no pude crear el archivo de descarga");
    } else if (rc != CURLE_OK) {
        resp.error = curl_error_text(rc, &resp.error_code);
    }
    /* Sin respuesta, status 0 (como WinHTTP cuando falla la conexión). */
    resp.status = (int)status;
    if (t.out) {
        fclose(t.out);
        if (resp.error) {
            char *p = wide_to_utf8(r->download_to);
            remove(p);
            free(p);
        }
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    resp.body_len = t.body.len;
    resp.body = t.body.data ? sb_steal(&t.body) : xstrdup("");
    return resp;
}

HttpResponse http_get(const char *url, int timeout_ms, bool browser_ua)
{
    HttpRequest r = {.method = "GET", .url = url, .timeout_ms = timeout_ms, .browser_ua = browser_ua};
    return http_request(&r);
}
