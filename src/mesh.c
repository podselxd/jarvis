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
/* Hilo que vigila Tailscale: levanta el servidor en cuanto se conecta (aunque
   haya sido después de abrir Sokari) y lo mueve si cambia de IP. */
static HANDLE g_watch, g_watch_quit;
static SRWLOCK g_ip_lock = SRWLOCK_INIT;
static char *g_listen_ip; /* donde escucha ahora, NULL si no escucha */

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

static void handle_client(SOCKET c, const char *origin)
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
        handle_client(c, name ? name : ip);
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
    /* También con el encabezado de antes, para tus PCs que todavía no se actualizan. */
    char *headers = str_printf("Content-Type: application/json\r\nX-Sokari-Secret: %s\r\n"
                               COMPAT_JARVIS_MESH_HEADER ": %s\r\n",
                               secret, secret);
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
        return "responde, pero el secreto de malla no coincide: copia el de una PC (Dispositivos → Copiar secreto) y "
               "pégalo en la otra";
    case MESH_NO_SOKARI:
        return "está en tu red, pero Sokari no le contesta: ábrelo en esa PC (si ya está abierto, espera a que su "
               "Tailscale se conecte)";
    case MESH_NOT_FOUND: return "no la encontré en tu red de Tailscale";
    case MESH_BAD_HOST: return "no tiene una dirección de Tailscale (100.x.y.z o un nombre .ts.net)";
    case MESH_ERROR: return "respondió con un error";
    default:
        return "no contesta: revisa que esté prendida, con Tailscale conectado y sin otra VPN prendida; si todo eso "
               "está bien, en esa PC abre Configuración → Dispositivos y dale «Permitir en el firewall»";
    }
}

MeshProbe mesh_probe(const char *host)
{
    if (!mesh_host_allowed(host)) return MESH_BAD_HOST;
    char *ip = resolve_tailscale(host);
    if (!ip) return MESH_NOT_FOUND;
    HttpResponse r = post_command(ip, "", 8000);
    free(ip);
    MeshProbe p = classify_response(&r);
    http_response_free(&r);
    return p;
}

/* ------------------------------------------------ tailscale y firewall --- */

/* Corre "tailscale status --json" y devuelve su salida (heap) o NULL. */
static char *tailscale_status_json(void)
{
    wchar_t *exe = expand_env(L"%ProgramFiles%\\Tailscale\\tailscale.exe");
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE};
    HANDLE rd = NULL, wr = NULL;
    char *out = NULL;
    if (file_exists(exe) && CreatePipe(&rd, &wr, &sa, 0)) {
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        wchar_t cmd[MAX_PATH + 32];
        swprintf(cmd, MAX_PATH + 32, L"\"%ls\" status --json", exe);
        STARTUPINFOW si = {sizeof si};
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = wr;
        si.hStdError = wr;
        PROCESS_INFORMATION pi;
        if (CreateProcessW(NULL, cmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) {
            CloseHandle(wr);
            wr = NULL;
            StrBuf sb;
            sb_init(&sb);
            char buf[4097];
            DWORD got;
            while (sb.len < (1u << 22) && ReadFile(rd, buf, sizeof buf - 1, &got, NULL) && got) {
                buf[got] = 0;
                sb_append(&sb, buf);
            }
            WaitForSingleObject(pi.hProcess, 5000);
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            out = sb.data;
        }
    }
    if (wr) CloseHandle(wr);
    if (rd) CloseHandle(rd);
    free(exe);
    return out;
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
    cJSON *j = json ? cJSON_Parse(json) : NULL;
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

int tailscale_windows_peers(MeshDevice **out)
{
    char *json = tailscale_status_json();
    int n = tailscale_parse_peers(json, out);
    free(json);
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
