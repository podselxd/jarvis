#define WIN32_LEAN_AND_MEAN
#include <windows.h> /* hilos y atómicos (en Linux, src/linux/include/windows.h) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "util.h"

#define LOG_ROTATE_BYTES (2 * 1024 * 1024)

static CRITICAL_SECTION g_lock;
static wchar_t *g_path;
static int g_console;
static volatile LONG g_ready;

void log_to_console(int enabled)
{
    g_console = enabled;
}

void log_init(const wchar_t *path)
{
    InitializeCriticalSection(&g_lock);
    g_path = xwcsdup(path);
    if (file_size(path) > LOG_ROTATE_BYTES) {
        wchar_t *old = xmalloc((wcslen(path) + 8) * sizeof(wchar_t));
        swprintf(old, wcslen(path) + 8, L"%ls.old", path);
        move_file(path, old);
        free(old);
    }
    InterlockedExchange(&g_ready, 1);
}

void log_msg(const char *fmt, ...)
{
    StrBuf sb;
    sb_init(&sb);
    char *when = format_epoch_local(now_epoch(), "%Y-%m-%d %H:%M:%S");
    sb_appendf(&sb, "[%s] ", when);
    free(when);
    va_list ap;
    va_start(ap, fmt);
    sb_vappendf(&sb, fmt, ap);
    va_end(ap);
#ifdef _WIN32
    sb_append(&sb, "\r\n");
#else
    sb_append(&sb, "\n");
#endif

    if (g_console) console_write(sb.data);
    if (InterlockedCompareExchange(&g_ready, 1, 1)) {
        EnterCriticalSection(&g_lock);
        append_file(g_path, sb.data, sb.len);
        LeaveCriticalSection(&g_lock);
    }
    sb_free(&sb);
}

static volatile LONG g_calls, g_in, g_out;

void turn_stats_reset(void)
{
    InterlockedExchange(&g_calls, 0);
    InterlockedExchange(&g_in, 0);
    InterlockedExchange(&g_out, 0);
}

void turn_stats_llm(int tokens_in, int tokens_out)
{
    InterlockedIncrement(&g_calls);
    InterlockedExchangeAdd(&g_in, tokens_in);
    InterlockedExchangeAdd(&g_out, tokens_out);
}

TurnStats turn_stats_get(void)
{
    TurnStats s = {(int)InterlockedCompareExchange(&g_calls, 0, 0), (int)InterlockedCompareExchange(&g_in, 0, 0),
                   (int)InterlockedCompareExchange(&g_out, 0, 0)};
    return s;
}
