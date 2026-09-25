/* Malla entre tus propios dispositivos vía Tailscale: un servidor HTTP mínimo
   que escucha SOLO en la IP de Tailscale de esta PC (nunca en 0.0.0.0), y
   exige el secreto de malla en cada pedido. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
/* Hilo que vigila Tailscale: levanta el servidor en cuanto se conecta (aunque
   haya sido después de abrir Sokari) y lo mueve si cambia de IP. */
static HANDLE g_watch, g_watch_quit;
static SRWLOCK g_ip_lock = SRWLOCK_INIT;
static char *g_listen_ip; /* donde escucha ahora, NULL si no escucha */

/* Dónde está tailscale.exe: donde lo deja su instalador o, si no, en el PATH
   (nunca en la carpeta actual). NULL si no está. */
static wchar_t *tailscale_exe(void)
{
    static const wchar_t *const CANDIDATES[] = {L"%ProgramFiles%\\Tailscale\\tailscale.exe",
                                                L"%ProgramW6432%\\Tailscale\\tailscale.exe",
                                                L"%ProgramFiles(x86)%\\Tailscale\\tailscale.exe"};
    for (size_t i = 0; i < sizeof CANDIDATES / sizeof *CANDIDATES; i++) {
        wchar_t *p = expand_env(CANDIDATES[i]);
        if (file_exists(p)) return p;
        free(p);
    }
    /* Lo pueden buscar a la vez el servidor y la ventana: nada estático. */
    DWORD cap = 32768;
    wchar_t *path = xmalloc(cap * sizeof *path), found[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"PATH", path, cap);
    wchar_t *r = n && n < cap && SearchPathW(path, L"tailscale.exe", NULL, MAX_PATH, found, NULL) ? xwcsdup(found) : NULL;
    free(path);
    return r;
}

bool tailscale_installed(void)
{
    wchar_t *p = tailscale_exe();
    bool ok = p != NULL;
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

static bool peer_is_mine(const char *ip);

static void handle_client(SOCKET c, const char *origin, const char *ip)
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
    char *expected = config_mesh_secret(false);
    char *given = secret ? xstrndup(secret, vlen) : xstrdup("");
    bool authorized = *expected && secure_equal(given, expected);
    free(given);
    free(expected);
    if (!authorized && peer_is_mine(ip)) {
        authorized = true;
        log_msg("Malla: %s es de tu misma cuenta de Tailscale, así que no hace falta el secreto.", origin);
    }
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
        char *reply = g_handler ? g_handler(comando, origin) : NULL;
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
    SOCKET listen_socket = (SOCKET)(ULONG_PTR)arg;
    while (InterlockedCompareExchange(&g_running, 1, 1)) {
        struct sockaddr_in peer = {0};
        int plen = sizeof peer;
        SOCKET c = accept(listen_socket, (struct sockaddr *)&peer, &plen);
        if (c == INVALID_SOCKET) {
            if (!InterlockedCompareExchange(&g_running, 1, 1)) break;
            Sleep(200);
            continue;
        }
        unsigned char *b = (unsigned char *)&peer.sin_addr;
        char ip[16];
        snprintf(ip, sizeof ip, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        char *name = mesh_device_name_for_ip(ip);
        handle_client(c, name ? name : ip, ip);
        free(name);
        shutdown(c, SD_BOTH);
        closesocket(c);
    }
    CoUninitialize();
    return 0;
}

static bool listen_on(const char *ip)
{
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(MESH_PORT);
    inet_pton(AF_INET, ip, &addr.sin_addr);
    BOOL excl = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&excl, sizeof excl);
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(ls, 8) != 0) {
        closesocket(ls);
        return false;
    }
    g_listen = ls;
    InterlockedExchange(&g_running, 1);
    g_thread = CreateThread(NULL, 0, server_thread, (LPVOID)(ULONG_PTR)ls, 0, NULL);
    AcquireSRWLockExclusive(&g_ip_lock);
    free(g_listen_ip);
    g_listen_ip = xstrdup(ip);
    ReleaseSRWLockExclusive(&g_ip_lock);
    log_msg("Servidor de malla escuchando en %s:%d (solo alcanzable por tu Tailscale).", ip, MESH_PORT);
    return true;
}

static void stop_listening(void)
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
    AcquireSRWLockExclusive(&g_ip_lock);
    free(g_listen_ip);
    g_listen_ip = NULL;
    ReleaseSRWLockExclusive(&g_ip_lock);
}

static DWORD WINAPI watch_thread(LPVOID arg)
{
    bool warned = false;
    do {
        char *ip = mesh_tailscale_ip();
        AcquireSRWLockShared(&g_ip_lock);
        bool listening = g_listen_ip != NULL, same = listening && ip && !strcmp(ip, g_listen_ip);
        ReleaseSRWLockShared(&g_ip_lock);
        if (listening && !same) {
            log_msg(ip ? "Tailscale cambió de IP: muevo el servidor de malla."
                       : "Tailscale se desconectó: el servidor de malla espera a que vuelva.");
            stop_listening();
            listening = false;
        }
        if (!listening && ip) {
            if (listen_on(ip)) {
                warned = false;
            } else if (!warned) {
                log_msg("No pude levantar el servidor de malla en %s:%d; lo vuelvo a intentar.", ip, MESH_PORT);
                warned = true;
            }
        } else if (!ip && !warned) {
            log_msg("Tailscale no está activo: el servidor de malla arranca solo en cuanto se conecte.");
            warned = true;
        }
        free(ip);
    } while (WaitForSingleObject(g_watch_quit, 15000) == WAIT_TIMEOUT);
    stop_listening();
    return 0;
}

bool mesh_start(MeshHandler handler)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    char *secret = config_mesh_secret(true); /* que exista desde el principio */
    free(secret);
    g_handler = handler;
    g_watch_quit = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_watch = CreateThread(NULL, 0, watch_thread, NULL, 0, NULL);
    return g_watch != NULL;
}

void mesh_stop(void)
{
    if (!g_watch) return;
    SetEvent(g_watch_quit);
    WaitForSingleObject(g_watch, 5000);
    CloseHandle(g_watch);
    CloseHandle(g_watch_quit);
    g_watch = g_watch_quit = NULL;
}

char *mesh_listening_ip(void)
{
    AcquireSRWLockShared(&g_ip_lock);
    char *ip = g_listen_ip ? xstrdup(g_listen_ip) : NULL;
    ReleaseSRWLockShared(&g_ip_lock);
    return ip;
}

bool mesh_listen_at(const char *ip, MeshHandler handler)
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_handler = handler;
    return listen_on(ip);
}

void mesh_listen_stop(void)
{
    stop_listening();
}

/* ------------------------------------------------------- dispositivos --- */

static cJSON *devices_load(void)
{
    wchar_t *df = local_file(L"dispositivos.json");
    cJSON *devs = json_load_object(df);
    free(df);
    return devs;
}

static void devices_save(cJSON *devs)
{
    wchar_t *df = local_file(L"dispositivos.json");
    json_save(df, devs);
    free(df);
}

int mesh_devices(MeshDevice **out)
{
    cJSON *devs = devices_load();
    int n = 0, cap = cJSON_GetArraySize(devs);
    MeshDevice *list = xcalloc((size_t)cap + 1, sizeof *list);
    cJSON *d;
    cJSON_ArrayForEach(d, devs)
    {
        if (!cJSON_IsString(d) || !d->string) continue;
        list[n].name = xstrdup(d->string);
        list[n].host = xstrdup(d->valuestring);
        n++;
    }
    cJSON_Delete(devs);
    *out = list;
    return n;
}

void mesh_devices_free(MeshDevice *list, int n)
{
    for (int i = 0; i < n; i++) {
        free(list[i].name);
        free(list[i].host);
    }
    free(list);
}

bool mesh_device_set(const char *name, const char *host)
{
    if (!*name || !mesh_host_allowed(host)) return false;
    cJSON *devs = devices_load();
    cJSON_DeleteItemFromObject(devs, name);
    cJSON_AddStringToObject(devs, name, host);
    devices_save(devs);
    cJSON_Delete(devs);
    return true;
}

bool mesh_device_remove(const char *name)
{
    cJSON *devs = devices_load();
    bool had = cJSON_GetObjectItem(devs, name) != NULL;
    cJSON_DeleteItemFromObject(devs, name);
    if (had) devices_save(devs);
    cJSON_Delete(devs);
    return had;
}

char *mesh_device_name_for_ip(const char *ip)
{
    cJSON *devs = devices_load();
    char *name = NULL;
    cJSON *d;
    cJSON_ArrayForEach(d, devs)
    {
        if (!name && cJSON_IsString(d) && !strcmp(d->valuestring, ip)) name = xstrdup(d->string);
    }
    cJSON_Delete(devs);
    return name;
}

/* --------------------------------------------------- probar / mandar --- */

static HttpResponse post_command(const char *ip, const char *comando, int timeout_ms)
{
    char *url = str_printf("http://%s:%d/comando", ip, MESH_PORT);
    cJSON *body = cJSON_CreateObject();
    cJSON_AddStringToObject(body, "comando", comando);
    char *payload = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    char *secret = config_mesh_secret(true);
    char *headers = str_printf("Content-Type: application/json\r\nX-Sokari-Secret: %s\r\n", secret);
    SecureZeroMemory(secret, strlen(secret));
    free(secret);
    /* Conectar tarda poco si la otra PC está; esperar su respuesta puede tardar
       (Sokari allá piensa y usa herramientas). */
    HttpRequest req = {.method = "POST", .url = url, .headers = headers, .body = payload, .body_len = strlen(payload),
                       .timeout_ms = timeout_ms, .connect_timeout_ms = 6000};
    HttpResponse resp = http_request(&req);
    SecureZeroMemory(headers, strlen(headers));
    free(headers);
    free(payload);
    free(url);
    return resp;
}

static MeshProbe classify_response(const HttpResponse *r)
{
    if (r->status == 200 || r->status == 400) return MESH_OK; /* 400: la orden vacía de la prueba */
    if (r->status == 503) return MESH_BUSY;
    if (r->status == 401) return MESH_BAD_SECRET;
    if (r->status != 0) return MESH_ERROR;
    if (r->error_code == ERROR_WINHTTP_CANNOT_CONNECT) return MESH_NO_SOKARI;
    return MESH_NO_ANSWER;
}

const char *mesh_probe_text(MeshProbe p)
{
    switch (p) {
    case MESH_OK: return "responde y el secreto coincide ✓";
    case MESH_BUSY: return "responde ✓ (ahorita está ocupado con otra cosa)";
    case MESH_BAD_SECRET:
        return "responde, pero no te reconoce: revisa que las dos PCs estén en la misma cuenta de Tailscale (o copia "
               "el secreto de una, con Copiar secreto, en la otra)";
    case MESH_NO_SOKARI:
        return "está en tu red, pero Sokari no le contesta: ábrelo en esa PC (si ya está abierto, espera a que su "
               "Tailscale se conecte)";
    case MESH_NOT_FOUND: return "no la encontré en tu red de Tailscale";
    case MESH_BAD_HOST: return "no tiene una dirección de Tailscale (100.x.y.z o un nombre .ts.net)";
    case MESH_ERROR: return "respondió con un error";
    default:
        return "no contesta: revisa que esté prendida, con Tailscale conectado y sin otra VPN prendida; si todo eso "
               "está bien, en esa PC abre Configuración → Dispositivos, dale «Permitir en el firewall» y luego "
               "«Revisar la malla»";
    }
}

MeshProbe mesh_probe(const char *host)
{
    if (!mesh_host_allowed(host)) return MESH_BAD_HOST;
    char *ip = resolve_tailscale(host);
    if (!ip) return MESH_NOT_FOUND;
    MeshProbe p = mesh_send_ip(ip, "", 8000, NULL);
    free(ip);
    return p;
}

MeshProbe mesh_send_ip(const char *ip, const char *comando, int timeout_ms, char **reply)
{
    HttpResponse r = post_command(ip, comando, timeout_ms);
    MeshProbe p = classify_response(&r);
    if (reply) *reply = r.status == 200 && r.body ? xstrdup(r.body) : NULL;
    http_response_free(&r);
    return p;
}

/* ------------------------------------------------ tailscale y firewall --- */

/* Lee lo que haya en el tubo; hasta el final si el proceso ya terminó. */
static void drain(HANDLE rd, StrBuf *sb, bool to_end)
{
    char buf[4097];
    for (;;) {
        DWORD avail = 0, got = 0;
        if (!to_end && (!PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) || !avail)) return;
        DWORD want = to_end || avail > sizeof buf - 1 ? sizeof buf - 1 : avail;
        if (sb->len > (1u << 22) || !ReadFile(rd, buf, want, &got, NULL) || !got) return;
        buf[got] = 0;
        sb_append(sb, buf);
    }
}

/* Corre exe con esos argumentos, sin ventana, y devuelve lo que imprime
   (heap; NULL si no arrancó). Lo que imprime como error va aparte en *err:
   mezclado, cualquier advertencia de Tailscale rompía su JSON. Si tarda más
   de timeout_ms, lo termina. */
static char *run_capture(const wchar_t *exe, const wchar_t *args, int timeout_ms, DWORD *exit_code, char **err)
{
    if (exit_code) *exit_code = (DWORD)-1;
    if (err) *err = NULL;
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE out_rd = NULL, out_wr = NULL, err_rd = NULL, err_wr = NULL;
    char *out = NULL;
    if (CreatePipe(&out_rd, &out_wr, &sa, 1 << 16) && CreatePipe(&err_rd, &err_wr, &sa, 1 << 16)) {
        SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_rd, HANDLE_FLAG_INHERIT, 0);
        size_t len = wcslen(exe) + wcslen(args) + 8;
        wchar_t *cmd = xmalloc(len * sizeof *cmd);
        swprintf(cmd, len, L"\"%ls\" %ls", exe, args);
        STARTUPINFOW si = {sizeof si};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = out_wr;
        si.hStdError = err_wr;
        PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(out_wr);
            CloseHandle(err_wr);
            out_wr = err_wr = NULL;
            StrBuf so, se;
            sb_init(&so);
            sb_init(&se);
            uint64_t end = GetTickCount64() + (uint64_t)timeout_ms;
            bool done = false;
            while (!done) {
                done = WaitForSingleObject(pi.hProcess, 20) != WAIT_TIMEOUT;
                if (!done && GetTickCount64() > end) {
                    TerminateProcess(pi.hProcess, 1);
                    WaitForSingleObject(pi.hProcess, 1000);
                    done = true;
                }
                drain(out_rd, &so, done);
            }
            drain(err_rd, &se, true);
            if (exit_code) GetExitCodeProcess(pi.hProcess, exit_code);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            out = so.data;
            if (err) *err = se.data;
            else sb_free(&se);
        }
        free(cmd);
    }
    if (out_wr) CloseHandle(out_wr);
    if (err_wr) CloseHandle(err_wr);
    if (out_rd) CloseHandle(out_rd);
    if (err_rd) CloseHandle(err_rd);
    return out;
}

static char *run_tailscale(const wchar_t *args, DWORD *exit_code, char **err)
{
    wchar_t *exe = tailscale_exe();
    char *out = exe ? run_capture(exe, args, 8000, exit_code, err) : NULL;
    if (!exe && exit_code) *exit_code = (DWORD)-1;
    if (!exe && err) *err = NULL;
    free(exe);
    return out;
}

/* El JSON empieza en la primera llave: antes puede venir una advertencia
   ("Warning: client version ... != tailscaled server version ..."). */
static const char *json_start(const char *s)
{
    return s ? strchr(s, '{') : NULL;
}

bool tailscale_parse_status(const char *out, TailscaleStatus *st)
{
    memset(st, 0, sizeof *st);
    cJSON *j = cJSON_Parse(json_start(out));
    if (!cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return false;
    }
    const char *state = cJSON_GetStringValue(cJSON_GetObjectItem(j, "BackendState"));
    st->state = xstrdup(state ? state : "");
    /* La cuenta: Self.UserID apunta a una entrada de User. */
    const cJSON *uid = cJSON_GetObjectItem(cJSON_GetObjectItem(j, "Self"), "UserID");
    const cJSON *u;
    cJSON_ArrayForEach(u, cJSON_GetObjectItem(j, "User"))
    {
        const cJSON *id = cJSON_GetObjectItem(u, "ID");
        const char *login = cJSON_GetStringValue(cJSON_GetObjectItem(u, "LoginName"));
        bool same = cJSON_IsNumber(uid) && cJSON_IsNumber(id) && !(id->valuedouble < uid->valuedouble) &&
                    !(id->valuedouble > uid->valuedouble);
        if (same && login && *login && !st->account) st->account = xstrdup(login);
    }
    st->peers = cJSON_GetArraySize(cJSON_GetObjectItem(j, "Peer"));
    cJSON_Delete(j);
    return true;
}

void tailscale_status_free(TailscaleStatus *st)
{
    free(st->state);
    free(st->account);
    memset(st, 0, sizeof *st);
}

char *tailscale_parse_whois_account(const char *out)
{
    cJSON *j = cJSON_Parse(json_start(out));
    const char *login = cJSON_GetStringValue(cJSON_GetObjectItem(cJSON_GetObjectItem(j, "UserProfile"), "LoginName"));
    char *r = login && *login ? xstrdup(login) : NULL;
    cJSON_Delete(j);
    return r;
}

static SRWLOCK g_account_lock = SRWLOCK_INIT;
static char *g_account;
static uint64_t g_account_at;

/* Tu cuenta de Tailscale en esta PC (heap) o NULL. Se vuelve a leer cada
   10 minutos. */
static char *my_account(void)
{
    uint64_t now = GetTickCount64();
    AcquireSRWLockExclusive(&g_account_lock);
    if (!g_account || now - g_account_at > 10 * 60 * 1000) {
        char *out = run_tailscale(L"status --json", NULL, NULL);
        TailscaleStatus st;
        if (tailscale_parse_status(out, &st) && st.account) {
            free(g_account);
            g_account = xstrdup(st.account);
            g_account_at = now;
        }
        tailscale_status_free(&st);
        free(out);
    }
    char *r = g_account ? xstrdup(g_account) : NULL;
    ReleaseSRWLockExclusive(&g_account_lock);
    return r;
}

/* ¿Esa IP de Tailscale es de un dispositivo de tu misma cuenta? Tailscale ya
   comprobó con criptografía quién manda cada paquete, así que entre tus
   propios dispositivos no hace falta compartir un secreto. Los dispositivos
   con etiqueta (sin dueño) nunca cuentan. */
static bool peer_is_mine(const char *ip)
{
    char *me = my_account();
    bool mine = false;
    if (me && strchr(me, '@')) {
        wchar_t args[64];
        swprintf(args, 64, L"whois --json %hs", ip);
        char *out = run_tailscale(args, NULL, NULL);
        char *owner = tailscale_parse_whois_account(out);
        mine = owner && !_stricmp(owner, me);
        free(owner);
        free(out);
    }
    free(me);
    return mine;
}

/* "LAPTOP-Ismael" -> "laptop-ismael": el nombre con el que se le habla. */
static char *device_name_from(const char *host_name)
{
    char *low = str_lower(host_name);
    StrBuf sb;
    sb_init(&sb);
    for (const char *p = low; *p; p++) {
        bool ok = isalnum((unsigned char)*p) || (unsigned char)*p >= 0x80;
        if (ok) sb_append_char(&sb, *p);
        else if (sb.len && sb.data[sb.len - 1] != '-') sb_append_char(&sb, '-');
    }
    while (sb.len && sb.data[sb.len - 1] == '-') sb.data[--sb.len] = 0;
    free(low);
    return sb.data;
}

int tailscale_parse_peers(const char *json, MeshDevice **out)
{
    cJSON *j = cJSON_Parse(json_start(json));
    cJSON *peers = cJSON_GetObjectItem(j, "Peer");
    int n = 0, cap = cJSON_GetArraySize(peers);
    MeshDevice *list = xcalloc((size_t)cap + 1, sizeof *list);
    cJSON *p;
    cJSON_ArrayForEach(p, peers)
    {
        const char *os = cJSON_GetStringValue(cJSON_GetObjectItem(p, "OS"));
        const char *hn = cJSON_GetStringValue(cJSON_GetObjectItem(p, "HostName"));
        if (!os || !hn || _stricmp(os, "windows")) continue; /* Sokari solo escucha en Windows */
        const cJSON *addr;
        const char *ip4 = NULL;
        cJSON_ArrayForEach(addr, cJSON_GetObjectItem(p, "TailscaleIPs"))
        {
            if (!ip4 && cJSON_IsString(addr) && mesh_host_allowed(addr->valuestring)) ip4 = addr->valuestring;
        }
        if (!ip4) continue;
        list[n].name = device_name_from(hn);
        list[n].host = xstrdup(ip4);
        if (!*list[n].name) {
            free(list[n].name);
            free(list[n].host);
            continue;
        }
        n++;
    }
    cJSON_Delete(j);
    *out = list;
    return n;
}

/* Por qué Detectar no encontró ninguna PC, en palabras para el usuario. */
static char *peers_problem(const char *out, DWORD code, const char *err)
{
    if (!tailscale_installed()) return xstrdup("No encontré Tailscale en esta PC: instálalo y entra con tu cuenta.");
    TailscaleStatus st;
    char *r;
    if (!tailscale_parse_status(out, &st)) {
        char *line = err ? xstrndup(err, strcspn(err, "\r\n")) : xstrdup("");
        r = *line ? str_printf("Tailscale no me contestó bien (código %lu: %s).", (unsigned long)code, line)
                  : str_printf("Tailscale no me contestó bien (código %lu).", (unsigned long)code);
        free(line);
        return r;
    }
    if (strcmp(st.state, "Running"))
        r = str_printf("Tailscale no está conectado en esta PC (estado: %s). Ábrelo y entra con tu cuenta.",
                       *st.state ? st.state : "desconocido");
    else if (!st.peers)
        r = str_printf("Tu cuenta de Tailscale (%s) no ve ningún otro dispositivo: en la otra PC entra a Tailscale con esa "
                       "misma cuenta.",
                       st.account ? st.account : "sin nombre");
    else
        r = str_printf("Tailscale ve %d dispositivo%s, pero ninguno con Windows. Dale a «Revisar la malla» para ver "
                       "cuáles son.",
                       st.peers, st.peers == 1 ? "" : "s");
    tailscale_status_free(&st);
    return r;
}

int tailscale_windows_peers(MeshDevice **out, char **why)
{
    DWORD code;
    char *err;
    char *json = run_tailscale(L"status --json", &code, &err);
    int n = tailscale_parse_peers(json, out);
    if (why) *why = n ? NULL : peers_problem(json, code, err);
    free(json);
    free(err);
    return n;
}

/* Abre el puerto de la malla en el firewall de Windows, solo para tu red de
   Tailscale. Antes borra las reglas de Sokari.exe que Windows haya creado
   (si alguna vez le diste "Cancelar" a su aviso, creó una que bloquea, y
   un bloqueo le gana a cualquier permiso). Pide permiso de administrador. */
bool mesh_allow_firewall(void)
{
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return false;
    wchar_t params[2 * MAX_PATH + 400];
    swprintf(params, sizeof params / sizeof *params,
             L"/c netsh advfirewall firewall delete rule name=all program=\"%ls\" >nul 2>&1 & "
             L"netsh advfirewall firewall add rule name=\"Sokari (malla)\" dir=in action=allow program=\"%ls\" "
             L"protocol=TCP localport=%d remoteip=100.64.0.0/10 profile=any enable=yes",
             exe, exe, MESH_PORT);
    SHELLEXECUTEINFOW sei = {sizeof sei};
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false; /* también si dijiste que no al permiso */
    DWORD code = 1;
    WaitForSingleObject(sei.hProcess, 30000);
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    log_msg(code == 0 ? "Firewall: la malla quedó permitida para tu red de Tailscale." : "Firewall: netsh falló (%lu).",
            (unsigned long)code);
    return code == 0;
}

/* ------------------------------------------------------ revisar malla --- */

/* Lo que ve tu red de Tailscale: "laptop (windows, en línea), pixel (android,
   desconectado)". Devuelve cuántos son. */
static int describe_peers(const char *out, StrBuf *seen)
{
    cJSON *j = cJSON_Parse(json_start(out));
    const cJSON *p;
    int n = 0;
    cJSON_ArrayForEach(p, cJSON_GetObjectItem(j, "Peer"))
    {
        const char *hn = cJSON_GetStringValue(cJSON_GetObjectItem(p, "HostName"));
        const char *os = cJSON_GetStringValue(cJSON_GetObjectItem(p, "OS"));
        bool online = cJSON_IsTrue(cJSON_GetObjectItem(p, "Online"));
        sb_appendf(seen, "%s%s (%s, %s)", n ? ", " : "", hn ? hn : "?", os && *os ? os : "?",
                   online ? "en línea" : "desconectado");
        n++;
    }
    cJSON_Delete(j);
    return n;
}

char *mesh_diagnose(void)
{
    StrBuf r;
    sb_init(&r);
    sb_appendf(&r, "Revisión de la malla (Sokari %s)\n\n", SOKARI_VERSION);

    wchar_t *exe = tailscale_exe();
    char *exe8 = exe ? wide_to_utf8(exe) : NULL;
    if (exe8) sb_appendf(&r, "✓ Tailscale instalado: %s\n", exe8);
    else sb_append(&r, "✗ No encontré Tailscale en esta PC. Instálalo (botón Instalar Tailscale) y entra con tu cuenta.\n");
    free(exe8);

    DWORD code = 0;
    char *err = NULL;
    char *out = exe ? run_capture(exe, L"status --json", 8000, &code, &err) : NULL;
    free(exe);
    TailscaleStatus st;
    bool parsed = tailscale_parse_status(out, &st);
    if (out && !parsed) {
        char *first = err ? xstrndup(err, strcspn(err, "\r\n")) : xstrdup("");
        sb_appendf(&r, "✗ Tailscale no contestó bien (código %lu)%s%s\n", (unsigned long)code, *first ? ": " : "",
                   first);
        free(first);
    } else if (!out && tailscale_installed()) {
        sb_append(&r, "✗ No pude correr tailscale.exe\n");
    } else if (parsed && strcmp(st.state, "Running")) {
        sb_appendf(&r, "✗ Tailscale no está conectado (estado: %s). Ábrelo y entra con tu cuenta.\n",
                   *st.state ? st.state : "desconocido");
    } else if (parsed) {
        sb_appendf(&r, "✓ Tailscale conectado con la cuenta %s\n", st.account ? st.account : "(sin nombre)");
    }
    free(err);

    char *ip = mesh_tailscale_ip(), *listening = mesh_listening_ip();
    if (ip) sb_appendf(&r, "✓ IP de esta PC en Tailscale: %s\n", ip);
    else sb_append(&r, "✗ No encontré la IP de Tailscale de esta PC (¿está conectado?)\n");
    if (listening) {
        sb_appendf(&r, "✓ Esta PC recibe órdenes en %s:%d\n", listening, MESH_PORT);
        MeshProbe p = mesh_send_ip(listening, "", 5000, NULL);
        if (p == MESH_OK) sb_append(&r, "✓ Prueba local: el servidor de esta PC contesta\n");
        else sb_appendf(&r, "✗ Prueba local: el servidor de esta PC %s\n", mesh_probe_text(p));
    } else {
        sb_append(&r, "✗ Esta PC todavía no recibe órdenes: se activa sola unos segundos después de que Tailscale se "
                      "conecta.\n");
    }
    free(ip);
    free(listening);

    wchar_t *netsh = expand_env(L"%SystemRoot%\\System32\\netsh.exe");
    DWORD fw = (DWORD)-1;
    free(run_capture(netsh, L"advfirewall firewall show rule name=\"Sokari (malla)\"", 8000, &fw, NULL));
    free(netsh);
    if (fw == 0) sb_append(&r, "✓ El firewall tiene la regla «Sokari (malla)»\n");
    else sb_append(&r, "✗ Falta la regla del firewall: dale «Permitir en el firewall» en esta PC\n");

    if (parsed) {
        StrBuf seen;
        sb_init(&seen);
        int n = describe_peers(out, &seen);
        if (n) sb_appendf(&r, "✓ Tu red de Tailscale ve %d dispositivo%s: %s\n", n, n == 1 ? "" : "s", seen.data);
        else
            sb_appendf(&r, "✗ Tu red de Tailscale no ve ningún otro dispositivo: en la otra PC entra a Tailscale con %s\n",
                       st.account ? st.account : "la misma cuenta");
        sb_free(&seen);
    }
    tailscale_status_free(&st);
    free(out);

    MeshDevice *devs;
    int nd = mesh_devices(&devs);
    if (!nd) sb_append(&r, "· Todavía no tienes PCs registradas: dale a «Detectar mis PCs».\n");
    for (int i = 0; i < nd; i++) {
        MeshProbe p = mesh_probe(devs[i].host);
        sb_appendf(&r, "%s %s (%s): %s\n", p == MESH_OK || p == MESH_BUSY ? "✓" : "✗", devs[i].name, devs[i].host,
                   mesh_probe_text(p));
    }
    mesh_devices_free(devs, nd);
    return r.data;
}

char *tool_registrar_dispositivo(const cJSON *a)
{
    char *nombre = str_lower(arg_str(a, "nombre"));
    char *n = str_trim(nombre);
    char *host = str_trim(arg_str(a, "host"));
    free(nombre);
    char *r;
    if (!*n || !*host)
        r = xstrdup("Necesito un nombre y la dirección de Tailscale de ese dispositivo.");
    else if (!mesh_device_set(n, host))
        r = xstrdup("Solo registro direcciones de Tailscale: una IP entre 100.64.0.0 y 100.127.255.255, o un "
                    "nombre que termine en .ts.net.");
    else
        r = str_printf("Listo, registré '%s' en %s.", n, host);
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
    cJSON *devs = devices_load();
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
        } else {
            HttpResponse resp = post_command(ip, comando, 90000);
            free(ip);
            MeshProbe p = classify_response(&resp);
            if (resp.status == 200) r = *resp.body ? xstrdup(resp.body) : xstrdup("Listo.");
            else if (p == MESH_BUSY) r = str_printf("'%s' está ocupado ahorita; prueba en un momento.", n);
            else if (p == MESH_ERROR) r = str_printf("'%s' respondió con un error (%d).", n, resp.status);
            else r = str_printf("No le llegó la orden a '%s': %s.", n, mesh_probe_text(p));
            http_response_free(&resp);
        }
    }
    cJSON_Delete(devs);
    free(n);
    return r;
}
