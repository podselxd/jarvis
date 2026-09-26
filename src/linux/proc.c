/* Programas externos para la versión de Linux (voces, sonidos, apps,
   Tailscale): siempre con posix_spawn y una lista de argumentos, nunca con un
   shell. */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "proc.h"
#include "util.h"

extern char **environ;

static uint64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

pid_t proc_spawn(const char *const argv[], int *in_fd, int *out_fd, int *err_fd, const char *const env_extra[])
{
    int pin[2] = {-1, -1}, pout[2] = {-1, -1}, perr[2] = {-1, -1};
    if ((in_fd && pipe2(pin, O_CLOEXEC)) || (out_fd && pipe2(pout, O_CLOEXEC)) || (err_fd && pipe2(perr, O_CLOEXEC))) {
        int fds[6] = {pin[0], pin[1], pout[0], pout[1], perr[0], perr[1]};
        for (int i = 0; i < 6; i++)
            if (fds[i] >= 0) close(fds[i]);
        return -1;
    }
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    if (in_fd) posix_spawn_file_actions_adddup2(&fa, pin[0], 0);
    else posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
    if (out_fd) posix_spawn_file_actions_adddup2(&fa, pout[1], 1);
    else posix_spawn_file_actions_addopen(&fa, 1, "/dev/null", O_WRONLY, 0);
    if (err_fd) posix_spawn_file_actions_adddup2(&fa, perr[1], 2);
    else posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    /* Un programa que se muere no se lleva a Sokari (SIGPIPE al escribirle). */
    posix_spawnattr_t at;
    posix_spawnattr_init(&at);
    sigset_t def;
    sigemptyset(&def);
    sigaddset(&def, SIGPIPE);
    sigaddset(&def, SIGINT);
    posix_spawnattr_setsigdefault(&at, &def);
    posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGDEF);
    char **env = environ;
    char **merged = NULL;
    if (env_extra && env_extra[0]) {
        size_t n = 0, k = 0;
        while (environ[n]) n++;
        while (env_extra[k]) k++;
        merged = xcalloc(n + k + 1, sizeof(char *));
        size_t m = 0;
        for (size_t i = 0; i < k; i++) merged[m++] = (char *)env_extra[i];
        for (size_t i = 0; i < n; i++) {
            bool replaced = false;
            for (size_t j = 0; j < k && !replaced; j++) {
                const char *eq = strchr(env_extra[j], '=');
                size_t len = eq ? (size_t)(eq - env_extra[j]) + 1 : 0;
                replaced = len && !strncmp(environ[i], env_extra[j], len);
            }
            if (!replaced) merged[m++] = environ[i];
        }
        env = merged;
    }
    pid_t pid = -1;
    int r = strchr(argv[0], '/') ? posix_spawn(&pid, argv[0], &fa, &at, (char *const *)argv, env)
                                 : posix_spawnp(&pid, argv[0], &fa, &at, (char *const *)argv, env);
    posix_spawn_file_actions_destroy(&fa);
    posix_spawnattr_destroy(&at);
    free(merged);
    if (in_fd) close(pin[0]);
    if (out_fd) close(pout[1]);
    if (err_fd) close(perr[1]);
    if (r != 0) {
        if (in_fd) close(pin[1]);
        if (out_fd) close(pout[0]);
        if (err_fd) close(perr[0]);
        return -1;
    }
    if (in_fd) *in_fd = pin[1];
    if (out_fd) *out_fd = pout[0];
    if (err_fd) *err_fd = perr[0];
    return pid;
}

bool proc_write_all(int fd, const void *data, size_t len)
{
    const char *p = data;
    while (len) {
        ssize_t w = write(fd, p, len);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return false;
        p += w;
        len -= (size_t)w;
    }
    return true;
}

int proc_finish(pid_t pid, int timeout_ms)
{
    if (pid <= 0) return -1;
    uint64_t until = mono_ms() + (uint64_t)(timeout_ms > 0 ? timeout_ms : 0);
    int st = 0;
    for (;;) {
        pid_t r = waitpid(pid, &st, WNOHANG);
        if (r == pid) return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        if (r < 0 && errno != EINTR) return -1;
        if (mono_ms() >= until) break;
        struct timespec ts = {0, 10 * 1000000L};
        nanosleep(&ts, NULL);
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {
    }
    return -1;
}

char *proc_run(const char *const argv[], const void *input, size_t input_len, int timeout_ms, size_t max_out,
               size_t *out_len, int *exit_code)
{
    int in = -1, out = -1;
    if (exit_code) *exit_code = -1;
    if (out_len) *out_len = 0;
    pid_t pid = proc_spawn(argv, input ? &in : NULL, &out, NULL, NULL);
    if (pid < 0) return NULL;
    if (input) {
        /* Entradas chicas (textos): caben en el pipe sin bloquear. */
        proc_write_all(in, input, input_len);
        close(in);
    }
    StrBuf sb;
    sb_init(&sb);
    uint64_t until = mono_ms() + (uint64_t)timeout_ms;
    bool timed_out = false;
    for (;;) {
        uint64_t now = mono_ms();
        if (now >= until) {
            timed_out = true;
            break;
        }
        struct pollfd p = {.fd = out, .events = POLLIN};
        int pr = poll(&p, 1, (int)(until - now));
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) {
            timed_out = pr == 0;
            break;
        }
        char buf[65536];
        ssize_t n = read(out, buf, sizeof buf);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (sb.len < max_out) sb_append_n(&sb, buf, sb.len + (size_t)n > max_out ? max_out - sb.len : (size_t)n);
    }
    close(out);
    int code = proc_finish(pid, timed_out ? 0 : 5000);
    if (exit_code) *exit_code = timed_out ? -1 : code;
    if (out_len) *out_len = sb.len;
    return sb.data ? sb_steal(&sb) : xstrdup("");
}

char *proc_which(const char *name)
{
    if (strchr(name, '/')) return access(name, X_OK) ? NULL : xstrdup(name);
    const char *path = getenv("PATH");
    if (!path || !*path) path = "/usr/local/bin:/usr/bin:/bin";
    char *copy = xstrdup(path), *save = NULL, *r = NULL;
    for (char *d = strtok_r(copy, ":", &save); d && !r; d = strtok_r(NULL, ":", &save)) {
        if (!*d || d[0] != '/') continue; /* nunca el directorio actual */
        char *full = str_printf("%s/%s", d, name);
        struct stat st;
        if (!stat(full, &st) && S_ISREG(st.st_mode) && !access(full, X_OK)) r = full;
        else free(full);
    }
    free(copy);
    return r;
}

char *proc_read_line(int fd, int timeout_ms)
{
    StrBuf sb;
    sb_init(&sb);
    uint64_t until = mono_ms() + (uint64_t)timeout_ms;
    for (;;) {
        uint64_t now = mono_ms();
        if (now >= until) break;
        struct pollfd p = {.fd = fd, .events = POLLIN};
        int pr = poll(&p, 1, (int)(until - now));
        if (pr < 0 && errno == EINTR) continue;
        if (pr <= 0) break;
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        if (c == '\n') {
            char *line = sb.data ? sb_steal(&sb) : xstrdup("");
            return line;
        }
        sb_append_char(&sb, c);
    }
    sb_free(&sb);
    return NULL;
}
