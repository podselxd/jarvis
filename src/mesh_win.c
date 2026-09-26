/* La malla en Windows: dónde está tailscale.exe y cómo se corre, la IP del
   adaptador de Tailscale y el firewall de Windows (por COM para leerlo sin
   permisos; netsh con permiso de administrador para abrirlo). */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <shellapi.h>
#include <oleauto.h>
#include <netfw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "mesh.h"
#include "mesh_os.h"
#include "util.h"

/* -1 sin revisar, 0 el firewall no deja entrar las órdenes, 1 sí. */
static volatile LONG g_fw_ok = -1;

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
                if (mesh_is_tailscale_v4(b) && tail) ip = str_printf("%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
            }
        }
    }
    free(addrs);
    return ip;
}

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

char *mesh_tailscale_path(void)
{
    wchar_t *exe = tailscale_exe();
    char *r = exe ? wide_to_utf8(exe) : NULL;
    free(exe);
    return r;
}

char *mesh_run_tailscale(const char *const args[], int timeout_ms, long *exit_code, char **err)
{
    if (exit_code) *exit_code = -1;
    if (err) *err = NULL;
    wchar_t *exe = tailscale_exe();
    if (!exe) return NULL;
    /* Los argumentos son palabras fijas o una IP ya revisada: sin espacios ni comillas. */
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; args[i]; i++) sb_appendf(&sb, "%s%s", i ? " " : "", args[i]);
    wchar_t *wargs = utf8_to_wide(sb.data ? sb.data : "");
    sb_free(&sb);
    DWORD code = (DWORD)-1;
    char *out = run_capture(exe, wargs, timeout_ms, &code, err);
    if (exit_code) *exit_code = (long)code;
    free(wargs);
    free(exe);
    return out;
}

/* ------------------------------------------------------------ firewall --- */

/* Lo que dice el firewall de Windows de Sokari.exe (por COM, sin permisos de
   administrador y sin depender del idioma de Windows). */
typedef struct {
    bool known;     /* se pudo leer */
    bool off;       /* el firewall de Windows está apagado */
    bool allow;     /* hay una regla que deja entrar a Sokari.exe */
    bool block;     /* hay una que lo bloquea (le gana a cualquier permiso) */
    bool block_all; /* «Bloquear todas las conexiones entrantes» */
} FwState;

#define FW_PROTOCOL_ANY 256 /* NET_FW_IP_PROTOCOL_ANY, que el netfw.h de MinGW no trae */
static const CLSID CLSID_FwPolicy2 = {0xE2B3C97F, 0x6AE1, 0x41AC, {0x81, 0x7A, 0xF6, 0xF9, 0x21, 0x66, 0xD7, 0xDD}};
static const IID IID_FwPolicy2 = {0x98325047, 0xC671, 0x4174, {0x8D, 0x81, 0xDE, 0xFC, 0xD3, 0xF0, 0x31, 0x86}};
static const IID IID_FwRule = {0xAF230D27, 0xBABA, 0x4E42, {0xAC, 0xED, 0xF5, 0x24, 0xF2, 0x2C, 0xFC, 0xE2}};

static void long_path(const wchar_t *in, wchar_t out[MAX_PATH])
{
    wchar_t tmp[MAX_PATH];
    if (!ExpandEnvironmentStringsW(in, tmp, MAX_PATH)) wcsncpy(tmp, in, MAX_PATH - 1), tmp[MAX_PATH - 1] = 0;
    if (!GetLongPathNameW(tmp, out, MAX_PATH)) wcscpy(out, tmp);
}

static void check_rule(INetFwRule *r, const wchar_t *exe, FwState *st)
{
    VARIANT_BOOL enabled = VARIANT_FALSE;
    NET_FW_RULE_DIRECTION dir = NET_FW_RULE_DIR_OUT;
    BSTR app = NULL;
    if (FAILED(INetFwRule_get_Enabled(r, &enabled)) || !enabled) return;
    if (FAILED(INetFwRule_get_Direction(r, &dir)) || dir != NET_FW_RULE_DIR_IN) return;
    if (FAILED(INetFwRule_get_ApplicationName(r, &app)) || !app) return;
    wchar_t rule_exe[MAX_PATH];
    long_path(app, rule_exe);
    SysFreeString(app);
    if (_wcsicmp(rule_exe, exe)) return;
    NET_FW_ACTION action = NET_FW_ACTION_ALLOW;
    long proto = FW_PROTOCOL_ANY;
    BSTR ports = NULL;
    INetFwRule_get_Action(r, &action);
    INetFwRule_get_Protocol(r, &proto);
    INetFwRule_get_LocalPorts(r, &ports);
    bool tcp = proto == NET_FW_IP_PROTOCOL_TCP || proto == FW_PROTOCOL_ANY;
    bool port = !ports || !*ports || !wcscmp(ports, L"*") || wcsstr(ports, L"8765");
    SysFreeString(ports);
    if (!tcp || !port) return;
    if (action == NET_FW_ACTION_BLOCK) st->block = true;
    else st->allow = true;
}

/* Recorre las reglas (unos cientos de milisegundos): no llamar en cada pedido. */
static FwState firewall_state(void)
{
    FwState st = {0};
    wchar_t raw[MAX_PATH], exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, raw, MAX_PATH)) return st;
    long_path(raw, exe);
    INetFwPolicy2 *pol = NULL;
    if (FAILED(CoCreateInstance(&CLSID_FwPolicy2, NULL, CLSCTX_INPROC_SERVER, &IID_FwPolicy2, (void **)&pol)))
        return st;
    long profiles = 0;
    INetFwPolicy2_get_CurrentProfileTypes(pol, &profiles);
    static const NET_FW_PROFILE_TYPE2 KINDS[] = {NET_FW_PROFILE2_DOMAIN, NET_FW_PROFILE2_PRIVATE, NET_FW_PROFILE2_PUBLIC};
    bool any_on = false;
    for (int i = 0; i < 3; i++) {
        if (!(profiles & KINDS[i])) continue;
        VARIANT_BOOL on = VARIANT_FALSE, block_all = VARIANT_FALSE;
        INetFwPolicy2_get_FirewallEnabled(pol, KINDS[i], &on);
        INetFwPolicy2_get_BlockAllInboundTraffic(pol, KINDS[i], &block_all);
        if (on) any_on = true;
        if (on && block_all) st.block_all = true;
    }
    st.off = !any_on;
    INetFwRules *rules = NULL;
    if (SUCCEEDED(INetFwPolicy2_get_Rules(pol, &rules))) {
        IUnknown *unk = NULL;
        IEnumVARIANT *en = NULL;
        if (SUCCEEDED(INetFwRules_get__NewEnum(rules, &unk)) &&
            SUCCEEDED(IUnknown_QueryInterface(unk, &IID_IEnumVARIANT, (void **)&en))) {
            VARIANT v;
            VariantInit(&v);
            ULONG got = 0;
            while (IEnumVARIANT_Next(en, 1, &v, &got) == S_OK && got) {
                INetFwRule *r = NULL;
                if (v.vt == VT_DISPATCH && v.pdispVal &&
                    SUCCEEDED(IDispatch_QueryInterface(v.pdispVal, &IID_FwRule, (void **)&r))) {
                    check_rule(r, exe, &st);
                    INetFwRule_Release(r);
                }
                VariantClear(&v);
            }
            st.known = true;
        }
        if (en) IEnumVARIANT_Release(en);
        if (unk) IUnknown_Release(unk);
        INetFwRules_Release(rules);
    }
    INetFwPolicy2_Release(pol);
    return st;
}

/* 1 si el firewall de Windows deja entrar las órdenes, 0 si no, -1 si no se sabe. */
static int firewall_ok(const FwState *st)
{
    if (!st->known) return -1;
    if (st->off) return 1;
    return st->allow && !st->block && !st->block_all ? 1 : 0;
}

int mesh_firewall_ok(void)
{
    return (int)InterlockedCompareExchange(&g_fw_ok, 0, 0);
}

int mesh_firewall_check(void)
{
    FwState st = firewall_state();
    int ok = firewall_ok(&st);
    InterlockedExchange(&g_fw_ok, ok);
    return ok;
}

/* Al empezar a escuchar: si el firewall no deja entrar las órdenes, pide
   permiso de administrador UNA sola vez por exe (si dices que no, no vuelve
   a insistir; el botón «Permitir en el firewall» sigue ahí). */
void mesh_firewall_on_listen(void)
{
    FwState st = firewall_state();
    int ok = firewall_ok(&st);
    InterlockedExchange(&g_fw_ok, ok);
    if (ok != 0 || st.block_all) return; /* bloquear todo lo quita el usuario en Windows, no un permiso */
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(NULL, exe, MAX_PATH)) return;
    char *exe8 = wide_to_utf8(exe), *asked = config_fw_asked();
    if (_stricmp(exe8, asked)) {
        config_set_fw_asked(exe8);
        log_msg(st.block ? "Firewall: Windows tiene una regla que bloquea a Sokari; pido permiso (una sola vez) para quitarla."
                         : "Firewall: todavía no deja entrar las órdenes de tus otras PCs; pido permiso (una sola vez).");
        if (mesh_allow_firewall()) InterlockedExchange(&g_fw_ok, 1);
    } else {
        log_msg("Firewall: sigue sin dejar entrar las órdenes (ya pedí permiso una vez; está el botón «Permitir en el "
                "firewall»).");
    }
    free(exe8);
    free(asked);
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
    if (code == 0) InterlockedExchange(&g_fw_ok, 1);
    return code == 0;
}

/* Los firewalls de antivirus (McAfee, Norton…) que Windows conoce, separados
   por comas; "" si no hay ninguno, NULL si no se pudo preguntar. */
static char *other_firewalls(void)
{
    wchar_t *ps = expand_env(L"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    DWORD code = (DWORD)-1;
    char *out = run_capture(ps,
                            L"-NoProfile -NonInteractive -Command \"Get-CimInstance -Namespace root/SecurityCenter2 "
                            L"-ClassName FirewallProduct | ForEach-Object { $_.displayName }\"",
                            10000, &code, NULL);
    free(ps);
    if (!out || code != 0) {
        free(out);
        return NULL;
    }
    StrBuf sb;
    sb_init(&sb);
    for (char *line = strtok(out, "\r\n"); line; line = strtok(NULL, "\r\n")) {
        char *t = str_trim(line);
        char *low = str_lower(t);
        /* El de Windows no cuenta: ese ya se revisó arriba. */
        if (*t && !strstr(low, "windows") && !strstr(low, "microsoft")) sb_appendf(&sb, "%s%s", sb.len ? ", " : "", t);
        free(low);
        free(t);
    }
    free(out);
    return sb.data ? sb.data : xstrdup("");
}


/* Los renglones del firewall en «Revisar la malla». */
void mesh_diagnose_firewall(StrBuf *r)
{
    FwState fw = firewall_state();
    InterlockedExchange(&g_fw_ok, firewall_ok(&fw));
    if (!fw.known) {
        wchar_t *netsh = expand_env(L"%SystemRoot%\\System32\\netsh.exe");
        DWORD fw_code = (DWORD)-1;
        free(run_capture(netsh, L"advfirewall firewall show rule name=\"Sokari (malla)\"", 8000, &fw_code, NULL));
        free(netsh);
        if (fw_code == 0) sb_append(r, "✓ El firewall tiene la regla «Sokari (malla)»\n");
        else sb_append(r, "✗ Falta la regla del firewall: dale «Permitir en el firewall» en esta PC\n");
    } else if (fw.off) {
        sb_append(r, "✓ El firewall de Windows está apagado: no bloquea la malla\n");
    } else if (fw.block_all) {
        sb_append(r, "✗ Windows está bloqueando TODAS las conexiones entrantes: en Seguridad de Windows → Firewall, "
                      "quita «Bloquear todas las conexiones entrantes» (si no, nadie te puede mandar órdenes)\n");
    } else if (fw.block) {
        sb_append(r, "✗ El firewall de Windows tiene una regla que BLOQUEA a Sokari (pasa si alguna vez le diste "
                      "«Cancelar» a su aviso): dale «Permitir en el firewall» y la quito\n");
    } else if (fw.allow) {
        sb_append(r, "✓ El firewall de Windows deja pasar las órdenes (regla «Sokari (malla)»)\n");
    } else {
        sb_append(r, "✗ Falta permitir a Sokari en el firewall: dale «Permitir en el firewall» en esta PC\n");
    }
    char *others = other_firewalls();
    if (others && *others)
        sb_appendf(r, "✗ Tienes otro firewall: %s. Ese no usa la regla de Windows: en su configuración permite a "
                       "Sokari.exe (TCP 8765) o apágalo\n",
                   others);
    else if (others)
        sb_append(r, "✓ No hay otro firewall (de antivirus) aparte del de Windows\n");
    free(others);
}
