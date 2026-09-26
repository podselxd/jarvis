/* Lo poquito de Windows que usa el código compartido (hilos, candados,
   eventos, tiempo y nombres de teclas), hecho con POSIX para compilarlo en
   Linux sin tocar la versión de Windows. No es un Windows de mentira: solo lo
   que ese código necesita, con la misma forma de usarse. Lo propio de cada
   sistema (audio, voces, ventanas, red…) vive aparte, en src/linux/. */
#ifndef SOKARI_LINUX_WINDOWS_H
#define SOKARI_LINUX_WINDOWS_H

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>

#define WINAPI
#define CALLBACK
#define APIENTRY

typedef int BOOL;
typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint32_t ULONG;
typedef int64_t LONG64;
typedef int64_t LONGLONG;
typedef uint64_t ULONGLONG;
typedef unsigned int UINT;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t INT_PTR;
typedef void *LPVOID;
typedef void *HANDLE;
typedef void *HWND;
typedef wchar_t WCHAR;
typedef const wchar_t *LPCWSTR;
typedef long HRESULT;

#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define MAX_PATH 4096
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define ERROR_TIMEOUT 1460u
#define S_OK 0L
#define S_FALSE 1L
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)
#define FAILED(hr) ((HRESULT)(hr) < 0)
#define COINIT_APARTMENTTHREADED 2
#define COINIT_MULTITHREADED 0
static inline HRESULT CoInitializeEx(void *reserved, DWORD flags)
{
    (void)reserved, (void)flags;
    return S_OK;
}
static inline void CoUninitialize(void) {}

/* ------------------------------------------------------------- errores --- */

extern __thread DWORD lx_last_error;
static inline DWORD GetLastError(void) { return lx_last_error; }
static inline void SetLastError(DWORD e) { lx_last_error = e; }

/* -------------------------------------------------------------- tiempo --- */

static inline ULONGLONG GetTickCount64(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (ULONGLONG)ts.tv_sec * 1000u + (ULONGLONG)ts.tv_nsec / 1000000u;
}

static inline void Sleep(DWORD ms)
{
    struct timespec ts = {(time_t)(ms / 1000), (long)(ms % 1000) * 1000000L};
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) {
    }
}

#define SecureZeroMemory(p, n) explicit_bzero((p), (n))
#define ZeroMemory(p, n) memset((p), 0, (n))
#define localtime_s(tm, t) (localtime_r((t), (tm)) ? 0 : EINVAL)

/* ------------------------------------------------------------ cadenas --- */

#define strtok_s strtok_r
#define _stricmp strcasecmp
#define _strnicmp strncasecmp
#define _wcsicmp wcscasecmp
#define _wcsnicmp wcsncasecmp

/* Mayúsculas y minúsculas de texto ancho: ASCII, acentos y ñ (Latin-1 y
   Latin Extended-A) sin depender del locale; lo demás, con el de la libc. */
wchar_t lx_towlower(wchar_t c);
wchar_t lx_towupper(wchar_t c);
static inline DWORD CharLowerBuffW(WCHAR *s, DWORD n)
{
    for (DWORD i = 0; i < n; i++) s[i] = lx_towlower(s[i]);
    return n;
}
static inline DWORD CharUpperBuffW(WCHAR *s, DWORD n)
{
    for (DWORD i = 0; i < n; i++) s[i] = lx_towupper(s[i]);
    return n;
}

/* ------------------------------------------------------------ sistema --- */

typedef struct {
    DWORD dwNumberOfProcessors;
} SYSTEM_INFO;
void GetSystemInfo(SYSTEM_INFO *si);

/* ---------------------------------------------------------- atómicos --- */

#define InterlockedExchange(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedExchange64(p, v) __atomic_exchange_n((p), (v), __ATOMIC_SEQ_CST)
#define InterlockedIncrement(p) __atomic_add_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedDecrement(p) __atomic_sub_fetch((p), 1, __ATOMIC_SEQ_CST)
#define InterlockedExchangeAdd(p, v) __atomic_fetch_add((p), (v), __ATOMIC_SEQ_CST)
static inline LONG lx_cas32(volatile LONG *p, LONG exchange, LONG comparand)
{
    __atomic_compare_exchange_n(p, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand; /* el valor que había */
}
static inline LONG64 lx_cas64(volatile LONG64 *p, LONG64 exchange, LONG64 comparand)
{
    __atomic_compare_exchange_n(p, &comparand, exchange, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return comparand;
}
#define InterlockedCompareExchange(p, e, c) lx_cas32((p), (e), (c))
#define InterlockedCompareExchange64(p, e, c) lx_cas64((p), (e), (c))

/* ----------------------------------------------------------- candados --- */

typedef struct {
    pthread_rwlock_t rw;
} SRWLOCK;
#define SRWLOCK_INIT {PTHREAD_RWLOCK_INITIALIZER}
static inline void InitializeSRWLock(SRWLOCK *l) { pthread_rwlock_init(&l->rw, NULL); }
static inline void AcquireSRWLockExclusive(SRWLOCK *l) { pthread_rwlock_wrlock(&l->rw); }
static inline void ReleaseSRWLockExclusive(SRWLOCK *l) { pthread_rwlock_unlock(&l->rw); }
static inline void AcquireSRWLockShared(SRWLOCK *l) { pthread_rwlock_rdlock(&l->rw); }
static inline void ReleaseSRWLockShared(SRWLOCK *l) { pthread_rwlock_unlock(&l->rw); }

/* Como en Windows, la misma hebra puede volver a entrar. */
typedef struct {
    pthread_mutex_t m;
} CRITICAL_SECTION;
void InitializeCriticalSection(CRITICAL_SECTION *cs);
static inline BOOL InitializeCriticalSectionAndSpinCount(CRITICAL_SECTION *cs, DWORD spins)
{
    (void)spins;
    InitializeCriticalSection(cs);
    return TRUE;
}
static inline void DeleteCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_destroy(&cs->m); }
static inline void EnterCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_lock(&cs->m); }
static inline void LeaveCriticalSection(CRITICAL_SECTION *cs) { pthread_mutex_unlock(&cs->m); }
static inline BOOL TryEnterCriticalSection(CRITICAL_SECTION *cs) { return pthread_mutex_trylock(&cs->m) == 0; }

typedef struct {
    pthread_cond_t c;
} CONDITION_VARIABLE;
#define CONDITION_VARIABLE_INIT {PTHREAD_COND_INITIALIZER}
static inline void InitializeConditionVariable(CONDITION_VARIABLE *cv) { pthread_cond_init(&cv->c, NULL); }
static inline void WakeAllConditionVariable(CONDITION_VARIABLE *cv) { pthread_cond_broadcast(&cv->c); }
static inline void WakeConditionVariable(CONDITION_VARIABLE *cv) { pthread_cond_signal(&cv->c); }
BOOL SleepConditionVariableCS(CONDITION_VARIABLE *cv, CRITICAL_SECTION *cs, DWORD ms);

/* ------------------------------------------------ eventos y hebras --- */

typedef DWORD(WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
HANDLE CreateEventW(void *sa, BOOL manual_reset, BOOL initial, const wchar_t *name);
BOOL SetEvent(HANDLE h);
BOOL ResetEvent(HANDLE h);
HANDLE CreateThread(void *sa, size_t stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, DWORD *tid);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
BOOL CloseHandle(HANDLE h);

/* -------------------------------------------- lo que usan las pruebas --- */

#define CP_UTF8 65001
static inline BOOL SetConsoleOutputCP(UINT cp)
{
    (void)cp;
    return TRUE;
}
DWORD GetTempPathW(DWORD n, wchar_t *buf);
BOOL DeleteFileW(const wchar_t *path);
BOOL RemoveDirectoryW(const wchar_t *path);

/* ---------------------------------------------- nombres de las teclas --- */

/* Los mismos números que en Windows: keys.c los usa como nombre de cada
   tecla, y cada sistema los traduce a sus teclas de verdad. */
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_LWIN 0x5B
#define VK_APPS 0x5D
#define VK_F1 0x70
#define VK_F12 0x7B
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE

#endif
