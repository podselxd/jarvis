/* La malla en Linux: el tailscale del sistema, la IP de su interfaz
   (tailscale0) y el firewall: ufw en Ubuntu, firewalld en Fedora. Tailscale
   mete sus propias reglas, así que casi siempre deja pasar lo que llega por
   su red; aun así se revisa, se dice, y «sokari --permitir-firewall» abre el
   puerto de la malla solo para tu red de Tailscale (pide tu contraseña). */
#include <windows.h>

#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "linux/proc.h"
#include "log.h"
#include "mesh.h"
#include "mesh_os.h"
#include "util.h"

/* -1 sin revisar (o no se puede saber), 0 el firewall no deja entrar las órdenes, 1 sí. */
static volatile LONG g_fw_ok = -1;

/* La regla de firewalld: el puerto de la malla, solo desde tu red de Tailscale. */
#define RICH_RULE "rule family=\"ipv4\" source address=\"100.64.0.0/10\" port port=\"8765\" protocol=\"tcp\" accept"

char *mesh_tailscale_path(void)
{
    char *p = proc_which("tailscale");
    if (p) return p;
    static const char *const CANDIDATES[] = {"/usr/bin/tailscale", "/usr/sbin/tailscale", "/usr/local/bin/tailscale",
                                             "/snap/bin/tailscale"};
    for (size_t i = 0; i < sizeof CANDIDATES / sizeof *CANDIDATES; i++)
        if (!access(CANDIDATES[i], X_OK)) return xstrdup(CANDIDATES[i]);
    return NULL;
}

bool tailscale_installed(void)
{
    char *p = mesh_tailscale_path();
    bool ok = p != NULL;
    free(p);
    return ok;
}

/* Corre argv sin shell y junta lo que imprime y, aparte, sus errores. */
static char *run_capture(const char *const argv[], int timeout_ms, long *exit_code, char **err)
{
    if (exit_code) *exit_code = -1;
    if (err) *err = NULL;
    int out = -1, er = -1;
    pid_t pid = proc_spawn(argv, NULL, &out, &er, NULL);
    if (pid < 0) return NULL;
    StrBuf so, se;
    sb_init(&so);
    sb_init(&se);
    ULONGLONG until = GetTickCount64() + (ULONGLONG)timeout_ms;
    bool timed_out = false;
    while (out >= 0 || er >= 0) {
        ULONGLONG now = GetTickCount64();
        if (now >= until) {
            timed_out = true;
            break;
        }
        struct pollfd p[2] = {{.fd = out, .events = POLLIN}, {.fd = er, .events = POLLIN}};
        int pr = poll(p, 2, (int)(until - now));
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) {
            timed_out = pr == 0;
            break;
        }
        for (int i = 0; i < 2; i++) {
            if (p[i].fd < 0 || !(p[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char buf[16384];
            ssize_t n = read(p[i].fd, buf, sizeof buf);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                close(p[i].fd);
                if (i == 0) out = -1;
                else er = -1;
                continue;
            }
            StrBuf *sb = i == 0 ? &so : &se;
            if (sb->len < (4u << 20)) sb_append_n(sb, buf, (size_t)n);
        }
    }
    if (out >= 0) close(out);
    if (er >= 0) close(er);
    int code = proc_finish(pid, timed_out ? 0 : 5000);
    if (exit_code) *exit_code = timed_out ? -1 : code;
    if (err) *err = se.data ? sb_steal(&se) : NULL;
    else sb_free(&se);
    return so.data ? sb_steal(&so) : xstrdup("");
}

char *mesh_run_tailscale(const char *const args[], int timeout_ms, long *exit_code, char **err)
{
    if (exit_code) *exit_code = -1;
    if (err) *err = NULL;
    char *exe = mesh_tailscale_path();
    if (!exe) return NULL;
    const char *argv[16];
    int n = 0;
    argv[n++] = exe;
    for (int i = 0; args[i] && n < 15; i++) argv[n++] = args[i];
    argv[n] = NULL;
    char *out = run_capture(argv, timeout_ms, exit_code, err);
    free(exe);
    return out;
}

/* La IP de Tailscale (100.64.0.0/10) de su interfaz, sin correr "tailscale ip". */
char *mesh_tailscale_ip(void)
{
    struct ifaddrs *ifs = NULL;
    if (getifaddrs(&ifs)) return NULL;
    char *ip = NULL;
    for (struct ifaddrs *i = ifs; i && !ip; i = i->ifa_next) {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !(i->ifa_flags & IFF_UP)) continue;
        if (!i->ifa_name || !strstr(i->ifa_name, "tailscale")) continue;
        unsigned char *b = (unsigned char *)&((struct sockaddr_in *)i->ifa_addr)->sin_addr;
        if (mesh_is_tailscale_v4(b)) ip = str_printf("%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
    }
    freeifaddrs(ifs);
    return ip;
}

/* ------------------------------------------------------------ firewall --- */

/* ¿ufw está prendido? Su configuración la puede leer cualquiera (sus reglas,
   solo root). */
static bool ufw_active(void)
{
    FILE *f = fopen("/etc/ufw/ufw.conf", "r");
    if (!f) return false;
    char line[256];
    bool on = false;
    while (fgets(line, sizeof line, f)) {
        char *t = str_trim(line);
        if (!strncmp(t, "ENABLED=", 8)) on = !strncasecmp(t + 8, "yes", 3) || !strncasecmp(t + 8, "\"yes", 4);
        free(t);
    }
    fclose(f);
    return on;
}

static int firewall_cmd(const char *const args[], char **out_text)
{
    const char *argv[8];
    int n = 0;
    argv[n++] = "firewall-cmd";
    for (int i = 0; args[i] && n < 7; i++) argv[n++] = args[i];
    argv[n] = NULL;
    long code = -1;
    char *out = run_capture(argv, 8000, &code, NULL);
    if (out_text) *out_text = out;
    else free(out);
    return out ? (int)code : -1;
}

static bool firewalld_running(void)
{
    char *fc = proc_which("firewall-cmd");
    bool has = fc != NULL;
    free(fc);
    if (!has) return false;
    static const char *const A[] = {"--state", NULL};
    return firewall_cmd(A, NULL) == 0;
}

/* ¿"8765" cae en "1025-65535/tcp" o "8765/tcp"? */
static bool ports_cover(const char *list)
{
    char *copy = xstrdup(list ? list : "");
    bool yes = false;
    for (char *save = NULL, *p = strtok_r(copy, " \n", &save); p && !yes; p = strtok_r(NULL, " \n", &save)) {
        int lo = 0, hi = 0;
        char proto[8] = "";
        bool range = sscanf(p, "%d-%d/%7s", &lo, &hi, proto) == 3;
        if (!range && sscanf(p, "%d/%7s", &lo, proto) == 2) {
            hi = lo;
            range = true;
        }
        yes = range && !strcmp(proto, "tcp") && lo <= MESH_PORT && MESH_PORT <= hi;
    }
    free(copy);
    return yes;
}

/* 1 si firewalld deja entrar la malla (la regla de Sokari, la zona de
   tailscale0 confía en todo, o el puerto está abierto, como en Fedora
   Workstation, que abre 1025-65535), 0 si no, -1 si no se pudo leer. */
static int firewalld_allows(void)
{
    static const char *const RULE[] = {"--query-rich-rule=" RICH_RULE, NULL};
    int q = firewall_cmd(RULE, NULL);
    if (q == 0) return 1;
    if (q != 1) return -1;
    char *zone = NULL;
    static const char *const IFZONE[] = {"--get-zone-of-interface=tailscale0", NULL};
    static const char *const DEFZONE[] = {"--get-default-zone", NULL};
    if (firewall_cmd(IFZONE, &zone) != 0) {
        free(zone);
        zone = NULL;
        if (firewall_cmd(DEFZONE, &zone) != 0) {
            free(zone);
            return -1;
        }
    }
    char *z = str_trim(zone);
    free(zone);
    int r = -1;
    if (!strcmp(z, "trusted")) {
        r = 1;
    } else if (*z) {
        char *zarg = str_printf("--zone=%s", z), *ports = NULL;
        const char *const LIST[] = {zarg, "--list-ports", NULL};
        if (firewall_cmd(LIST, &ports) == 0) r = ports_cover(ports) ? 1 : 0;
        free(ports);
        free(zarg);
    }
    free(z);
    return r;
}

int mesh_firewall_check(void)
{
    bool ufw = ufw_active(), fwd = firewalld_running();
    /* Con ufw no se sabe sin root; con Tailscale, casi siempre deja pasar. */
    int ok = !ufw && !fwd ? 1 : fwd ? firewalld_allows() : -1;
    if (ok == 1 && ufw) ok = -1;
    InterlockedExchange(&g_fw_ok, ok);
    return ok;
}

int mesh_firewall_ok(void)
{
    return (int)InterlockedCompareExchange(&g_fw_ok, 0, 0);
}

/* En Linux no se pide la contraseña solo al abrir: se avisa en el registro y
   «sokari --permitir-firewall» lo hace cuando tú quieras. */
void mesh_firewall_on_listen(void)
{
    if (mesh_firewall_check() == 0)
        log_msg("Firewall: firewalld no deja entrar las órdenes de tus otras PCs; «sokari --permitir-firewall» abre el "
                "puerto de la malla solo para tu red de Tailscale.");
}

bool mesh_allow_firewall(void)
{
    bool ok = true, any = false;
    if (firewalld_running()) {
        any = true;
        /* firewall-cmd pide tu contraseña (polkit) y solo abre la malla para 100.64.0.0/10. */
        static const char *const ADD[] = {"--permanent", "--add-rich-rule=" RICH_RULE, NULL};
        static const char *const RELOAD[] = {"--reload", NULL};
        ok = firewall_cmd(ADD, NULL) == 0 && firewall_cmd(RELOAD, NULL) == 0;
        log_msg(ok ? "Firewall: firewalld deja entrar la malla desde tu red de Tailscale."
                   : "Firewall: no pude agregar la regla de la malla a firewalld.");
    }
    if (ufw_active()) {
        any = true;
        const char *argv[] = {"pkexec", "ufw", "allow", "in", "on", "tailscale0", "to", "any", "port", "8765", "proto",
                              "tcp", "comment", "Sokari (malla)", NULL};
        long code = -1;
        free(run_capture(argv, 120000, &code, NULL));
        bool ufw_ok = code == 0;
        log_msg(ufw_ok ? "Firewall: ufw deja entrar la malla por Tailscale (tailscale0)."
                       : "Firewall: no pude agregar la regla de la malla a ufw.");
        ok = ok && ufw_ok;
    }
    if (!any) log_msg("Firewall: no hay ufw ni firewalld prendidos; nada que abrir.");
    if (ok) InterlockedExchange(&g_fw_ok, 1);
    return ok;
}

void mesh_diagnose_firewall(StrBuf *r)
{
    bool ufw = ufw_active(), fwd = firewalld_running();
    if (!ufw && !fwd) {
        sb_append(r, "✓ No hay firewall prendido (ni ufw ni firewalld): no bloquea la malla\n");
        InterlockedExchange(&g_fw_ok, 1);
        return;
    }
    int ok = -1;
    if (fwd) {
        ok = firewalld_allows();
        if (ok == 1)
            sb_append(r, "✓ firewalld deja entrar las órdenes de tu red de Tailscale\n");
        else if (ok == 0)
            sb_append(r, "✗ firewalld no deja entrar las órdenes: corre «sokari --permitir-firewall» (pide tu "
                         "contraseña) y lo abre solo para tu red de Tailscale\n");
        else
            sb_append(r, "· firewalld está prendido y no pude leer sus reglas: si otra PC no te llega, corre «sokari "
                         "--permitir-firewall»\n");
    }
    if (ufw) {
        sb_append(r, "· ufw está prendido. Tailscale normalmente deja pasar lo que llega por su red; si otra PC no te "
                     "llega, corre «sokari --permitir-firewall» (pide tu contraseña)\n");
        if (ok == 1) ok = -1;
    }
    InterlockedExchange(&g_fw_ok, ok);
}
