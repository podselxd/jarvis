#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (GetFileAttributesExW(path, GetFileExInfoStandard, &info) &&
        (((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow) > LOG_ROTATE_BYTES) {
        wchar_t *old = xmalloc((wcslen(path) + 8) * sizeof(wchar_t));
        swprintf(old, wcslen(path) + 8, L"%ls.old", path);
        MoveFileExW(path, old, MOVEFILE_REPLACE_EXISTING);
        free(old);
    }
    InterlockedExchange(&g_ready, 1);
}

void log_msg(const char *fmt, ...)
{
    StrBuf sb;
    sb_init(&sb);
    SYSTEMTIME st;
    GetLocalTime(&st);
    sb_appendf(&sb, "[%04d-%02d-%02d %02d:%02d:%02d] ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
               st.wSecond);
    va_list ap;
    va_start(ap, fmt);
    sb_vappendf(&sb, fmt, ap);
    va_end(ap);
    sb_append(&sb, "\r\n");

    if (g_console) {
        wchar_t *w = utf8_to_wide(sb.data);
        DWORD wrote;
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out && out != INVALID_HANDLE_VALUE) {
            if (!WriteConsoleW(out, w, (DWORD)wcslen(w), &wrote, NULL))
                WriteFile(out, sb.data, (DWORD)sb.len, &wrote, NULL);
        }
        free(w);
    }
    if (InterlockedCompareExchange(&g_ready, 1, 1)) {
        EnterCriticalSection(&g_lock);
        append_file(g_path, sb.data, sb.len);
        LeaveCriticalSection(&g_lock);
    }
    sb_free(&sb);
}
