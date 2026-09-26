/* net.h con WinHTTP (el mismo que hace las peticiones), shlwapi y Winsock. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shlwapi.h>
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>

#include "net.h"
#include "util.h"

char *url_host(const char *url)
{
    wchar_t *w = utf8_to_wide(url);
    URL_COMPONENTS uc = {0};
    uc.dwStructSize = sizeof uc;
    wchar_t host[512];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 512;
    char *r = NULL;
    if (WinHttpCrackUrl(w, 0, 0, &uc) && (uc.nScheme == INTERNET_SCHEME_HTTP || uc.nScheme == INTERNET_SCHEME_HTTPS) &&
        uc.dwHostNameLength) {
        host[uc.dwHostNameLength] = 0;
        r = wide_to_utf8(host);
    }
    free(w);
    return r;
}

char *url_resolve(const char *base, const char *rel)
{
    wchar_t *b = utf8_to_wide(base), *r = utf8_to_wide(rel);
    wchar_t next[4096];
    DWORD len = 4096;
    bool ok = SUCCEEDED(UrlCombineW(b, r, next, &len, 0));
    free(b);
    free(r);
    return ok ? wide_to_utf8(next) : NULL;
}

int net_resolve(const char *host, NetAddr *out, int max)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    wchar_t *h = utf8_to_wide(host);
    ADDRINFOW hints = {0}, *ai = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int n = -1;
    if (GetAddrInfoW(h, NULL, &hints, &ai) == 0 && ai) {
        n = 0;
        for (ADDRINFOW *p = ai; p && n < max; p = p->ai_next) {
            if (p->ai_addr->sa_family == AF_INET) {
                out[n].family = 4;
                memcpy(out[n].b, &((const struct sockaddr_in *)p->ai_addr)->sin_addr, 4);
            } else if (p->ai_addr->sa_family == AF_INET6) {
                out[n].family = 6;
                memcpy(out[n].b, ((const struct sockaddr_in6 *)p->ai_addr)->sin6_addr.s6_addr, 16);
            } else {
                out[n].family = 0; /* otra familia: se trata como privada */
            }
            n++;
        }
        FreeAddrInfoW(ai);
    }
    free(h);
    WSACleanup();
    return n;
}
