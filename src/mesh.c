/* Malla entre tus propios dispositivos vía Tailscale: un servidor HTTP mínimo
   que escucha SOLO en la IP de Tailscale de esta PC (nunca en 0.0.0.0), y
   exige el secreto de malla en cada pedido. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat_jarvis.h"
#include "config.h"
#include "http.h"
#include "log.h"
#include "memory.h"
#include "mesh.h"
#include "tools.h"
#include "util.h"

#define MESH_PORT 8765
#define MAX_HEADER 16384
#define MAX_BODY 65536

static SOCKET g_listen = INVALID_SOCKET;
static HANDLE g_thread;
static MeshHandler g_handler;
static volatile LONG g_running;

bool tailscale_installed(void)
{
    wchar_t *p = expand_env(L"%ProgramFiles%\\Tailscale\\tailscale.exe");
    bool ok = file_exists(p);
    free(p);
    return ok;
}

static bool is_tailscale_v4(const unsigned char b[4])
{
    return b[0] == 100 && b[1] >= 64 && b[1] <= 127;
}

/* La IP de Tailscale está en 100.64.0.0/10 (CGNAT); se busca en el adaptador
   de Tailscale directamente, sin tener que ejecutar "tailscale ip". */
char *mesh_tailscale_ip(void)
{
    ULONG size = 32 * 1024;
    IP_ADAPTER_ADDRESSES *addrs = xmalloc(size);
    ULONG rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                    NULL, addrs, &size);
    if (rc == ERROR_BUFFER_OVERFLOW) {
        addrs = xrealloc(addrs, size);
        rc = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                  NULL, addrs, &size);
    }
    char *ip = NULL;
    if (rc == NO_ERROR) {
        for (IP_ADAPTER_ADDRESSES *a = addrs; a && !ip; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            bool tail = (a->FriendlyName && wcsstr(a->FriendlyName, L"Tailscale")) ||
                        (a->Description && wcsstr(a->Description, L"Tailscale"));
            for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u && !ip; u = u->Next) {
                struct sockaddr_in *sin = (struct sockaddr_in *)u->Address.lpSockaddr;
                unsigned char *b = (unsigned char *)&sin->sin_addr;
                if (is_tailscale_v4(b) && tail) ip = str_printf("%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            }
        }
    }
    free(addrs);
    return ip;
}

/* Solo direcciones de Tailscale: una IP 100.64.0.0/10 o un nombre MagicDNS
   *.ts.net. Así el secreto de la malla (que viaja en cada pedido) nunca sale
   hacia una dirección cualquiera que el modelo haya registrado. */
bool mesh_host_allowed(const char *host)
{
    unsigned v[4];
    char extra;
    if (sscanf(host, "%u.%u.%u.%u%c", &v[0], &v[1], &v[2], &v[3], &extra) == 4) {
        if (v[0] > 255 || v[1] > 255 || v[2] > 255 || v[3] > 255) return false;
        unsigned char b[4] = {(unsigned char)v[0], (unsigned char)v[1], (unsigned char)v[2], (unsigned char)v[3]};
        return is_tailscale_v4(b);
    }
    size_t n = strlen(host);
    if (n <= 7 || n > 253) return false;
    for (const char *p = host; *p; p++)
        if (!(isalnum((unsigned char)*p) || *p == '.' || *p == '-')) return false;
    char *low = str_lower(host);
    bool ok = host[0] != '.' && host[0] != '-' && !strstr(host, "..") && str_ends_with(low, ".ts.net");
    free(low);
    return ok;
}

/* Resuelve host y devuelve su IP de Tailscale ("100.x.y.z"), o NULL si no
   resuelve o si alguna de sus direcciones está fuera de 100.64.0.0/10. */
static char *resolve_tailscale(const char *host)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa)) return NULL;
    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char *ip = NULL;
    if (getaddrinfo(host, NULL, &hints, &res) == 0) {
        bool all_ok = true;
        for (struct addrinfo *r = res; r; r = r->ai_next) {
            unsigned char *b = (unsigned char *)&((struct sockaddr_in *)r->ai_addr)->sin_addr;
            if (!is_tailscale_v4(b)) all_ok = false;
            else if (!ip) ip = str_printf("%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        }
        if (!all_ok) {
            free(ip);
            ip = NULL;
        }
        freeaddrinfo(res);
    }
    WSACleanup();
    return ip;
}

static void send_all(SOCKET s, const char *data, size_t len)
{
    while (len) {
        int n = send(s, data, (int)(len > 65536 ? 65536 : len), 0);
        if (n <= 0) return;
        data += n;
        len -= (size_t)n;
    }
}

static void respond(SOCKET s, int code, const char *reason, const char *body)
{
    char *msg = str_printf("HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %zu\r\n"
                           "Connection: close\r\n\r\n%s",
                           code, reason, strlen(body), body);
    send_all(s, msg, strlen(msg));
    free(msg);
}

static const char *find_header(const char *headers, const char *name, size_t *len)
{
    size_t n = strlen(name);
    for (const char *p = headers; p && *p;) {
        const char *eol = strstr(p, "\r\n");
        if (!eol) break;
        if ((size_t)(eol - p) > n && !_strnicmp(p, name, n) && p[n] == ':') {
            const char *v = p + n + 1;
            while (*v == ' ') v++;
            *len = (size_t)(eol - v);
            return v;
        }
        p = eol + 2;
    }
    return NULL;
}

static void handle_client(SOCKET c)
{
    DWORD timeout = 10000;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout, sizeof timeout);
    char *buf = xmalloc(MAX_HEADER + MAX_BODY + 1);
    size_t got = 0;
    char *hdr_end = NULL;
    while (got < MAX_HEADER && !hdr_end) {
        int n = recv(c, buf + got, (int)(MAX_HEADER - got), 0);
        if (n <= 0) break;
        got += (size_t)n;
        buf[got] = 0;
        hdr_end = strstr(buf, "\r\n\r\n");
    }
    if (!hdr_end) {
        respond(c, 400, "Bad Request", "pedido inválido");
        free(buf);
        return;
    }
    size_t hlen = (size_t)(hdr_end - buf) + 4;
    if (strncmp(buf, "POST /comando ", 14) != 0) {
        respond(c, 404, "Not Found", "no existe");
        free(buf);
        return;
    }
    size_t vlen = 0;
    const char *secret = find_header(buf, "X-Sokari-Secret", &vlen);
    if (!secret) secret = find_header(buf, COMPAT_JARVIS_MESH_HEADER, &vlen);
    char *expected = config_mesh_secret(false);
    char *given = secret ? xstrndup(secret, vlen) : xstrdup("");
    bool authorized = *expected && secure_equal(given, expected);
    free(given);
    free(expected);
    if (!authorized) {
        respond(c, 401, "Unauthorized", "secreto invalido");
        free(buf);
        return;
    }
    const char *cl = find_header(buf, "Content-Length", &vlen);
    size_t body_len = cl ? (size_t)strtoul(cl, NULL, 10) : 0;
    if (body_len > MAX_BODY) {
        respond(c, 413, "Payload Too Large", "pedido demasiado grande");
        free(buf);
        return;
    }
    while (got < hlen + body_len) {
        int n = recv(c, buf + got, (int)(hlen + body_len - got), 0);
        if (n <= 0) break;
        got += (size_t)n;
    }
    buf[hlen + body_len < got ? hlen + body_len : got] = 0;
    cJSON *j = cJSON_Parse(buf + hlen);
    cJSON *cmd = j ? cJSON_GetObjectItem(j, "comando") : NULL;
    char *comando = cJSON_IsString(cmd) ? str_trim(cmd->valuestring) : xstrdup("");
    cJSON_Delete(j);
    if (!*comando) {
        respond(c, 400, "Bad Request", "falta el comando");
    } else {
        char *reply = g_handler ? g_handler(comando) : NULL;
        if (reply) respond(c, 200, "OK", *reply ? reply : "Listo.");
        else respond(c, 503, "Service Unavailable", "Sokari está ocupado ahora, prueba en un momento.");
        free(reply);
    }
    free(comando);
    free(buf);
}

static DWORD WINAPI server_thread(LPVOID arg)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        SOCKET c = accept(g_listen, NULL, NULL);
        if (c == INVALID_SOCKET) {
            if (!InterlockedCompareExchange(&g_running, 1, 1)) break;
            Sleep(200);
            continue;
        }
        handle_client(c);
        shutdown(c, SD_BOTH);
        closesocket(c);
    }
    CoUninitialize();
    return 0;
}

bool mesh_start(MeshHandler handler)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    char *ip = mesh_tailscale_ip();
    if (!ip) {
        log_msg("Tailscale no está activo: el servidor de malla no arranca (Sokari sigue normal).");
        return false;
    }
    char *secret = config_mesh_secret(true);
    free(secret);
    g_handler = handler;
    g_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MESH_PORT);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    BOOL excl = TRUE;
    setsockopt(g_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&excl, sizeof excl);
    if (bind(g_listen, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(g_listen, 8) != 0) {
        log_msg("No pude levantar el servidor de malla en %s:%d.", ip, MESH_PORT);
        closesocket(g_listen);
        g_listen = INVALID_SOCKET;
        free(ip);
        return false;
    }
    InterlockedExchange(&g_running, 1);
    g_thread = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
    log_msg("Servidor de malla escuchando en %s:%d (solo alcanzable por tu Tailscale).", ip, MESH_PORT);
    free(ip);
    return true;
}

void mesh_stop(void)
{
    if (g_listen == INVALID_SOCKET) return;
    InterlockedExchange(&g_running, 0);
    closesocket(g_listen);
    g_listen = INVALID_SOCKET;
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}

char *tool_registrar_dispositivo(const cJSON *a)
{
    char *nombre = str_lower(arg_str(a, "nombre"));
    char *n = str_trim(nombre);
    char *host = str_trim(arg_str(a, "host"));
    free(nombre);
    if (!*n || !*host) {
        free(n);
        free(host);
        return xstrdup("Necesito un nombre y la dirección de Tailscale de ese dispositivo.");
    }
    if (!mesh_host_allowed(host)) {
        free(n);
        free(host);
        return xstrdup("Solo registro direcciones de Tailscale: una IP entre 100.64.0.0 y 100.127.255.255, o un "
                       "nombre que termine en .ts.net.");
    }
    wchar_t *df = local_file(L"dispositivos.json");
    cJSON *devs = json_load_object(df);
    cJSON_DeleteItemFromObject(devs, n);
    cJSON_AddStringToObject(devs, n, host);
    json_save(df, devs);
    cJSON_Delete(devs);
    free(df);
    char *r = str_printf("Listo, registré '%s' en %s.", n, host);
    free(n);
    free(host);
    return r;
}

char *tool_gestionar_dispositivo(const cJSON *a)
{
    char *nombre = str_lower(arg_str(a, "nombre"));
    char *n = str_trim(nombre);
    free(nombre);
    const char *comando = arg_str(a, "comando");
    wchar_t *df = local_file(L"dispositivos.json");
    cJSON *devs = json_load_object(df);
    free(df);
    cJSON *host = cJSON_GetObjectItem(devs, n);
    char *r;
    if (!cJSON_IsString(host)) {
        StrBuf known;
        sb_init(&known);
        cJSON *d;
        cJSON_ArrayForEach(d, devs) sb_appendf(&known, "%s%s", known.len ? ", " : "", d->string);
        r = str_printf("No tengo registrado un dispositivo llamado '%s'. Los que conozco: %s.", n,
                       known.len ? known.data : "ninguno todavía");
        sb_free(&known);
    } else if (str_is_blank(comando)) {
        r = xstrdup("No me dijiste qué comando mandarle.");
    } else if (!mesh_host_allowed(host->valuestring)) {
        r = str_printf("'%s' no tiene una dirección de Tailscale (%s), así que no le mando nada. Vuelve a registrarlo "
                       "con su IP 100.x.y.z o su nombre .ts.net.",
                       n, host->valuestring);
    } else {
        /* Se conecta a la IP ya revisada y no al nombre: si el DNS cambiara de
           respuesta entre la revisión y la conexión, igual queda en Tailscale. */
        char *ip = resolve_tailscale(host->valuestring);
        if (!ip) {
            r = str_printf("No pude encontrar a '%s' en tu red de Tailscale (¿está prendido y conectado?).", n);
            goto done;
        }
        char *url = str_printf("http://%s:%d/comando", ip, MESH_PORT);
        free(ip);
        cJSON *body = cJSON_CreateObject();
        cJSON_AddStringToObject(body, "comando", comando);
        char *payload = cJSON_PrintUnformatted(body);
        cJSON_Delete(body);
        char *secret = config_mesh_secret(true);
        /* También con el encabezado de antes, para tus PCs que todavía no se actualizan. */
        char *headers = str_printf("Content-Type: application/json\r\nX-Sokari-Secret: %s\r\n"
                                   COMPAT_JARVIS_MESH_HEADER ": %s\r\n",
                                   secret, secret);
        free(secret);
        HttpRequest req = {.method = "POST", .url = url, .headers = headers, .body = payload,
                           .body_len = strlen(payload), .timeout_ms = 30000};
        HttpResponse resp = http_request(&req);
        if (resp.status == 0) r = str_printf("No pude conectarme con '%s': %s", n, resp.error ? resp.error : "sin respuesta");
        else if (resp.status == 401) r = str_printf("'%s' rechazó el pedido (el secreto de malla no coincide).", n);
        else if (resp.status != 200) r = str_printf("'%s' respondió con un error (%d).", n, resp.status);
        else r = *resp.body ? xstrdup(resp.body) : xstrdup("Listo.");
        http_response_free(&resp);
        free(headers);
        free(payload);
        free(url);
    }
done:
    cJSON_Delete(devs);
    free(n);
    return r;
}
