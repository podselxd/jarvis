/* Hebras, eventos y variables de condición al estilo de Windows, con pthreads
   (ver include/windows.h). */
#include <windows.h>

#include <limits.h>
#include <stdio.h>

#include "util.h"

#include <unistd.h>

__thread DWORD lx_last_error;

wchar_t lx_towlower(wchar_t c)
{
    if (c >= L'A' && c <= L'Z') return c + 32;
    if (c < 0x80) return c;
    if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
    if (c >= 0x100 && c <= 0x137 && !(c & 1)) return c + 1;
    if (c >= 0x139 && c <= 0x148 && (c & 1)) return c + 1;
    if (c >= 0x14A && c <= 0x177 && !(c & 1)) return c + 1;
    if (c == 0x178) return 0xFF;
    if ((c == 0x179 || c == 0x17B || c == 0x17D)) return c + 1;
    return (wchar_t)towlower((wint_t)c);
}

wchar_t lx_towupper(wchar_t c)
{
    if (c >= L'a' && c <= L'z') return c - 32;
    if (c < 0x80) return c;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    if (c >= 0x101 && c <= 0x138 && (c & 1) && c != 0x131 && c != 0x138) return c - 1;
    if (c >= 0x13A && c <= 0x149 && !(c & 1)) return c - 1;
    if (c >= 0x14B && c <= 0x178 && (c & 1)) return c - 1;
    if ((c == 0x17A || c == 0x17C || c == 0x17E)) return c - 1;
    return (wchar_t)towupper((wint_t)c);
}

void GetSystemInfo(SYSTEM_INFO *si)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    si->dwNumberOfProcessors = n > 0 ? (DWORD)n : 1;
}

void InitializeCriticalSection(CRITICAL_SECTION *cs)
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&cs->m, &a);
    pthread_mutexattr_destroy(&a);
}

/* Los plazos de las condiciones y eventos se cuentan con el reloj monotónico:
   si cambia la hora de la PC, no se alargan ni se acortan. */
static void deadline_in(struct timespec *ts, DWORD ms)
{
    clock_gettime(CLOCK_MONOTONIC, ts);
    ts->tv_sec += (time_t)(ms / 1000);
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) ts->tv_sec++, ts->tv_nsec -= 1000000000L;
}

BOOL SleepConditionVariableCS(CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD ms)
{
    if (ms == INFINITE) return pthread_cond_wait(&cv->c, &cs->m) == 0;
    /* Una CONDITION_VARIABLE en ceros usa el reloj de pared (así inicia
       PTHREAD_COND_INITIALIZER); para el plazo se usa el mismo. */
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += (time_t)(ms / 1000);
    ts.tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) ts.tv_sec++, ts.tv_nsec -= 1000000000L;
    int r = pthread_cond_timedwait(&cv->c, &cs->m, &ts);
    if (r == ETIMEDOUT) {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    return r == 0;
}

/* Un evento, o la señal de que una hebra ya terminó. */
typedef struct {
    pthread_mutex_t m;
    pthread_cond_t c;
    bool manual, signaled;
    bool thread;
    int refs; /* una hebra: el HANDLE y la hebra misma */
    LPTHREAD_START_ROUTINE fn;
    void *arg;
} LxObject;

static LxObject *lx_new(bool manual, bool signaled)
{
    LxObject *o = calloc(1, sizeof *o);
    if (!o) abort();
    pthread_mutex_init(&o->m, NULL);
    pthread_condattr_t a;
    pthread_condattr_init(&a);
    pthread_condattr_setclock(&a, CLOCK_MONOTONIC);
    pthread_cond_init(&o->c, &a);
    pthread_condattr_destroy(&a);
    o->manual = manual;
    o->signaled = signaled;
    o->refs = 1;
    return o;
}

static void lx_unref(LxObject *o)
{
    pthread_mutex_lock(&o->m);
    int left = --o->refs;
    pthread_mutex_unlock(&o->m);
    if (left) return;
    pthread_mutex_destroy(&o->m);
    pthread_cond_destroy(&o->c);
    free(o);
}

HANDLE CreateEventW(void *sa, BOOL manual_reset, BOOL initial, const wchar_t *name)
{
    (void)sa, (void)name;
    return lx_new(manual_reset, initial);
}

BOOL SetEvent(HANDLE h)
{
    LxObject *o = h;
    if (!o) return FALSE;
    pthread_mutex_lock(&o->m);
    o->signaled = true;
    pthread_cond_broadcast(&o->c);
    pthread_mutex_unlock(&o->m);
    return TRUE;
}

BOOL ResetEvent(HANDLE h)
{
    LxObject *o = h;
    if (!o) return FALSE;
    pthread_mutex_lock(&o->m);
    o->signaled = false;
    pthread_mutex_unlock(&o->m);
    return TRUE;
}

static void *thread_main(void *p)
{
    LxObject *o = p;
    o->fn(o->arg);
    pthread_mutex_lock(&o->m);
    o->signaled = true;
    pthread_cond_broadcast(&o->c);
    pthread_mutex_unlock(&o->m);
    lx_unref(o);
    return NULL;
}

HANDLE CreateThread(void *sa, size_t stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, DWORD *tid)
{
    (void)sa, (void)flags;
    LxObject *o = lx_new(true, false);
    o->thread = true;
    o->fn = fn;
    o->arg = arg;
    o->refs = 2;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (stack) pthread_attr_setstacksize(&a, stack < (size_t)PTHREAD_STACK_MIN ? (size_t)PTHREAD_STACK_MIN : stack);
    pthread_t t;
    int r = pthread_create(&t, &a, thread_main, o);
    pthread_attr_destroy(&a);
    if (r) {
        o->refs = 1;
        lx_unref(o);
        return NULL;
    }
    if (tid) *tid = 0;
    return o;
}

DWORD WaitForSingleObject(HANDLE h, DWORD ms)
{
    LxObject *o = h;
    if (!o) return WAIT_FAILED;
    struct timespec ts;
    if (ms != INFINITE) deadline_in(&ts, ms);
    pthread_mutex_lock(&o->m);
    int r = 0;
    while (!o->signaled && r != ETIMEDOUT) r = ms == INFINITE ? pthread_cond_wait(&o->c, &o->m) : pthread_cond_timedwait(&o->c, &o->m, &ts);
    bool got = o->signaled;
    if (got && !o->manual) o->signaled = false;
    pthread_mutex_unlock(&o->m);
    return got ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}

BOOL CloseHandle(HANDLE h)
{
    if (!h) return FALSE;
    lx_unref(h);
    return TRUE;
}

DWORD GetTempPathW(DWORD n, wchar_t *buf)
{
    const char *t = getenv("TMPDIR");
    char *dir = str_printf("%s/", t && *t ? t : "/tmp");
    wchar_t *w = utf8_to_wide(dir);
    free(dir);
    DWORD len = (DWORD)wcslen(w);
    if (len < n) wcscpy(buf, w);
    free(w);
    return len < n ? len : len + 1;
}

BOOL DeleteFileW(const wchar_t *path)
{
    char *p = wide_to_utf8(path);
    BOOL ok = unlink(p) == 0;
    free(p);
    return ok;
}

BOOL RemoveDirectoryW(const wchar_t *path)
{
    char *p = wide_to_utf8(path);
    BOOL ok = rmdir(p) == 0;
    free(p);
    return ok;
}
