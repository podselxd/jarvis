/* net.h en Linux: las URLs se leen con el mismo analizador que usa curl para
   conectarse, y los nombres se resuelven con getaddrinfo. */
#include <curl/curl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "net.h"
#include "util.h"

char *url_host(const char *url)
{
    CURLU *u = curl_url();
    char *scheme = NULL, *host = NULL, *r = NULL;
    if (u && !curl_url_set(u, CURLUPART_URL, url, 0) && !curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) &&
        (!strcmp(scheme, "http") || !strcmp(scheme, "https")) && !curl_url_get(u, CURLUPART_HOST, &host, 0) && *host)
        r = xstrdup(host);
    curl_free(scheme);
    curl_free(host);
    curl_url_cleanup(u);
    return r;
}

char *url_resolve(const char *base, const char *rel)
{
    CURLU *u = curl_url();
    char *full = NULL, *r = NULL;
    /* Con la base puesta, una dirección relativa se arma sobre ella. */
    if (u && !curl_url_set(u, CURLUPART_URL, base, 0) && !curl_url_set(u, CURLUPART_URL, rel, CURLU_URLENCODE) &&
        !curl_url_get(u, CURLUPART_URL, &full, 0))
        r = xstrdup(full);
    curl_free(full);
    curl_url_cleanup(u);
    return r;
}

int net_resolve(const char *host, NetAddr *out, int max)
{
    struct addrinfo hints = {0}, *ai = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_IDN; /* dominios con acentos, como GetAddrInfoW en Windows */
    if (getaddrinfo(host, NULL, &hints, &ai) != 0 || !ai) return -1;
    int n = 0;
    for (struct addrinfo *p = ai; p && n < max; p = p->ai_next) {
        if (p->ai_family == AF_INET) {
            out[n].family = 4;
            memcpy(out[n].b, &((const struct sockaddr_in *)p->ai_addr)->sin_addr, 4);
        } else if (p->ai_family == AF_INET6) {
            out[n].family = 6;
            memcpy(out[n].b, ((const struct sockaddr_in6 *)p->ai_addr)->sin6_addr.s6_addr, 16);
        } else {
            out[n].family = 0;
        }
        n++;
    }
    freeaddrinfo(ai);
    return n;
}
