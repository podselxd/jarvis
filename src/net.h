#ifndef SOKARI_NET_H
#define SOKARI_NET_H

/* Lo de red que cambia con el sistema, para que la revisión de direcciones
   (tools_web.c) sea la misma en Windows y en Linux. */

/* El host de una URL http(s) tal como lo entiende el cliente HTTP de este
   sistema (así "http://x@127.0.0.1" no engaña), sin tocar. NULL si la URL no
   es http(s) o no se entiende (heap). */
char *url_host(const char *url);

/* La dirección a la que lleva rel (un Location de una redirección) desde
   base. NULL si no se puede armar (heap). */
char *url_resolve(const char *base, const char *rel);

/* Las IPs a las que resuelve host: IPv4 en 4 bytes (family 4) o IPv6 en 16
   (family 6), hasta max. Devuelve cuántas, o -1 si no resolvió. */
typedef struct {
    int family;
    unsigned char b[16];
} NetAddr;
int net_resolve(const char *host, NetAddr *out, int max);

#endif
