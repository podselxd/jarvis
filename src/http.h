#ifndef JARVIS_HTTP_H
#define JARVIS_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>

typedef struct {
    int status;        /* 0 = no hubo respuesta (error de red) */
    char *body;        /* siempre terminado en NUL */
    size_t body_len;
    char *content_type;
    int retry_after;   /* segundos del header Retry-After, -1 si no vino */
    int rl_remaining_tokens; /* x-ratelimit-remaining-tokens de Groq, -1 si no vino */
    double rl_reset_tokens;  /* segundos hasta que se repone ese cupo, -1 si no vino */
    char *error;       /* descripción del error de red, NULL si hubo respuesta */
} HttpResponse;

typedef struct {
    const char *method;
    const char *url;
    const char *headers; /* "Clave: valor\r\n" ... */
    const void *body;
    size_t body_len;
    int timeout_ms;
    size_t max_bytes;
    bool browser_ua;
    const wchar_t *download_to;
} HttpRequest;

bool http_init(void);
HttpResponse http_request(const HttpRequest *req);
HttpResponse http_get(const char *url, int timeout_ms, bool browser_ua);
void http_response_free(HttpResponse *r);
char *url_encode(const char *s);

#endif
