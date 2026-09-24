#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "http.h"
#include "log.h"
#include "util.h"

#ifndef WINHTTP_OPTION_DECOMPRESSION
#define WINHTTP_OPTION_DECOMPRESSION 118
#define WINHTTP_DECOMPRESSION_FLAG_ALL 3
#endif
#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

#define DEFAULT_MAX_BYTES (32u * 1024u * 1024u)

static HINTERNET g_api_session;
static HINTERNET g_web_session;

static HINTERNET open_session(const wchar_t *ua)
{
    HINTERNET s = WinHttpOpen(ua, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s)
        s = WinHttpOpen(ua, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (s) {
        DWORD flags = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(s, WINHTTP_OPTION_DECOMPRESSION, &flags, sizeof flags);
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
        if (!WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
            WinHttpSetOption(s, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
        }
    }
    return s;
}

bool http_init(void)
{
    g_api_session = open_session(L"Sokari/" JARVIS_VERSION_W " (Windows)");
    g_web_session = open_session(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                                 L"Chrome/128.0.0.0 Safari/537.36");
    return g_api_session && g_web_session;
}

void http_response_free(HttpResponse *r)
{
    free(r->body);
    free(r->content_type);
    free(r->error);
    free(r->location);
    memset(r, 0, sizeof *r);
}

static char *win_error_text(DWORD code)
{
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT: return xstrdup("se agotó el tiempo de espera");
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return xstrdup("no se pudo resolver el nombre (¿sin internet?)");
    case ERROR_WINHTTP_CANNOT_CONNECT: return xstrdup("no se pudo conectar");
    case ERROR_WINHTTP_CONNECTION_ERROR: return xstrdup("se cortó la conexión");
    case ERROR_WINHTTP_SECURE_FAILURE: return xstrdup("falló la conexión segura (TLS)");
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_UNRECOGNIZED_SCHEME: return xstrdup("la dirección no es válida");
    default: return str_printf("error de red %lu", (unsigned long)code);
    }
}

/* Duraciones estilo Go que manda Groq: "7.66s", "1m2.5s", "350ms". */
static double parse_duration(const wchar_t *s)
{
    double total = 0, num = 0, frac = 0;
    bool any = false;
    while (*s) {
        num = 0;
        frac = 0;
        double scale = 1;
        bool dot = false;
        while ((*s >= L'0' && *s <= L'9') || *s == L'.') {
            if (*s == L'.') dot = true;
            else if (dot) frac += (*s - L'0') * (scale /= 10);
            else num = num * 10 + (*s - L'0');
            s++;
            any = true;
        }
        num += frac;
        if (s[0] == L'm' && s[1] == L's') total += num / 1000, s += 2;
        else if (*s == L'h') total += num * 3600, s++;
        else if (*s == L'm') total += num * 60, s++;
        else if (*s == L's') total += num, s++;
        else if (*s) s++;
    }
    return any ? total : -1;
}

static void query_rate_limits(HINTERNET req, HttpResponse *resp)
{
    wchar_t buf[64];
    DWORD size = sizeof buf;
    resp->rl_remaining_tokens = -1;
    resp->rl_reset_tokens = -1;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"x-ratelimit-remaining-tokens", buf, &size, WINHTTP_NO_HEADER_INDEX))
        resp->rl_remaining_tokens = _wtoi(buf);
    size = sizeof buf;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"x-ratelimit-reset-tokens", buf, &size, WINHTTP_NO_HEADER_INDEX))
        resp->rl_reset_tokens = parse_duration(buf);
}

static int query_retry_after(HINTERNET req)
{
    wchar_t buf[64];
    DWORD size = sizeof buf;
    wcscpy(buf, L"Retry-After");
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, L"Retry-After", buf, &size, WINHTTP_NO_HEADER_INDEX))
        return -1;
    double v = _wtof(buf);
    if (v <= 0) return -1;
    return (int)(v + 0.999);
}

HttpResponse http_request(const HttpRequest *r)
{
    HttpResponse resp = {0};
    resp.retry_after = -1;
    resp.rl_remaining_tokens = -1;
    resp.rl_reset_tokens = -1;
    HINTERNET session = r->browser_ua ? g_web_session : g_api_session;
    if (!session) {
        resp.error = xstrdup("WinHTTP no está disponible");
        resp.body = xstrdup("");
        return resp;
    }

    wchar_t *wurl = utf8_to_wide(r->url);
    URL_COMPONENTS uc = {0};
    uc.dwStructSize = sizeof uc;
    wchar_t host[512], path[8192], extra[8192];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 512;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 8192;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 8192;
    HINTERNET conn = NULL, req = NULL;
    HANDLE out_file = INVALID_HANDLE_VALUE;
    StrBuf body;
    sb_init(&body);

    if (!WinHttpCrackUrl(wurl, 0, 0, &uc) || (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS)) {
        resp.error = xstrdup("la dirección no es válida");
        goto done;
    }
    host[uc.dwHostNameLength] = 0;
    size_t plen = uc.dwUrlPathLength, elen = uc.dwExtraInfoLength;
    wchar_t *object = xmalloc((plen + elen + 2) * sizeof(wchar_t));
    memcpy(object, path, plen * sizeof(wchar_t));
    memcpy(object + plen, extra, elen * sizeof(wchar_t));
    object[plen + elen] = 0;
    if (!object[0]) wcscpy(object, L"/");

    conn = WinHttpConnect(session, host, uc.nPort, 0);
    wchar_t *method = utf8_to_wide(r->method ? r->method : "GET");
    if (conn)
        req = WinHttpOpenRequest(conn, method, object, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    free(method);
    free(object);
    if (!req) {
        resp.error = win_error_text(GetLastError());
        goto done;
    }
    int t = r->timeout_ms > 0 ? r->timeout_ms : 60000;
    WinHttpSetTimeouts(req, t, t, t, t);
    if (r->no_redirects) {
        DWORD feature = WINHTTP_DISABLE_REDIRECTS;
        WinHttpSetOption(req, WINHTTP_OPTION_DISABLE_FEATURE, &feature, sizeof feature);
    }

    wchar_t *headers = r->headers ? utf8_to_wide(r->headers) : NULL;
    BOOL sent = WinHttpSendRequest(req, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, headers ? (DWORD)-1L : 0,
                                   (LPVOID)r->body, (DWORD)r->body_len, (DWORD)r->body_len, 0);
    free(headers);
    if (!sent || !WinHttpReceiveResponse(req, NULL)) {
        resp.error = win_error_text(GetLastError());
        goto done;
    }

    DWORD status = 0, size = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    resp.status = (int)status;
    resp.retry_after = query_retry_after(req);
    query_rate_limits(req, &resp);
    wchar_t ctype[256];
    size = sizeof ctype;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX, ctype, &size,
                            WINHTTP_NO_HEADER_INDEX))
        resp.content_type = wide_to_utf8(ctype);
    if (r->no_redirects && status >= 300 && status < 400) {
        DWORD lsize = 0;
        WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, NULL, &lsize,
                            WINHTTP_NO_HEADER_INDEX);
        if (lsize && lsize < 64 * 1024) {
            wchar_t *loc = xmalloc(lsize + sizeof(wchar_t));
            if (WinHttpQueryHeaders(req, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX, loc, &lsize,
                                    WINHTTP_NO_HEADER_INDEX))
                resp.location = wide_to_utf8(loc);
            free(loc);
        }
    }

    if (r->download_to && status == 200) {
        out_file = CreateFileW(r->download_to, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (out_file == INVALID_HANDLE_VALUE) {
            resp.error = xstrdup("no pude crear el archivo de descarga");
            goto done;
        }
    }

    size_t cap = r->max_bytes ? r->max_bytes : DEFAULT_MAX_BYTES;
    size_t total = 0;
    char chunk[65536];
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            resp.error = win_error_text(GetLastError());
            break;
        }
        if (!avail) break;
        while (avail) {
            DWORD want = avail > sizeof chunk ? (DWORD)sizeof chunk : avail;
            if (!WinHttpReadData(req, chunk, want, &got) || !got) {
                avail = 0;
                break;
            }
            avail -= got;
            total += got;
            if (out_file != INVALID_HANDLE_VALUE) {
                DWORD wrote;
                if (!WriteFile(out_file, chunk, got, &wrote, NULL) || wrote != got) {
                    resp.error = xstrdup("no pude escribir la descarga");
                    goto done;
                }
            } else if (body.len < cap) {
                size_t keep = got;
                if (body.len + keep > cap) keep = cap - body.len;
                sb_append_n(&body, chunk, keep);
            }
        }
        if (out_file == INVALID_HANDLE_VALUE && body.len >= cap) break;
    }
    (void)total;

done:
    if (out_file != INVALID_HANDLE_VALUE) {
        CloseHandle(out_file);
        if (resp.error) DeleteFileW(r->download_to);
    }
    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    free(wurl);
    resp.body_len = body.len;
    resp.body = sb_steal(&body);
    if (!resp.body) resp.body = xstrdup("");
    return resp;
}

HttpResponse http_get(const char *url, int timeout_ms, bool browser_ua)
{
    HttpRequest r = {.method = "GET", .url = url, .timeout_ms = timeout_ms, .browser_ua = browser_ua};
    return http_request(&r);
}

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
