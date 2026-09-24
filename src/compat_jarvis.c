/* Paso de la versión anterior (Jarvis) a Sokari. Temporal: se borra cuando
   tus equipos ya corran Sokari. Ver compat_jarvis.h. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autostart.h"
#include "compat_jarvis.h"
#include "config.h"
#include "log.h"
#include "ui.h"
#include "util.h"

#define OLD_NAME L"Jarvis"
#define OLD_MUTEX L"Local\\JarvisAsistenteDeVoz"
#define OLD_MSG_CLASS L"JarvisMessageWindow"
#define OLD_RUN_VALUE L"Jarvis"

wchar_t *compat_jarvis_dir(const wchar_t *base)
{
    return path_join(base, OLD_NAME);
}

/* Copia una carpeta completa sin pisar lo que ya exista del otro lado ni
   seguir enlaces (un enlace podría apuntar a cualquier parte del disco).
   Devuelve cuántos archivos copió. */
static int copy_tree(const wchar_t *src, const wchar_t *dst, const wchar_t *const *skip, int nskip)
{
    if (!ensure_dir(dst)) return 0;
    wchar_t *pattern = path_join(src, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int copied = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        bool skipped = false;
        for (int i = 0; i < nskip && !skipped; i++) skipped = !_wcsicmp(fd.cFileName, skip[i]);
        if (skipped) continue;
        wchar_t *s = path_join(src, fd.cFileName), *d = path_join(dst, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) copied += copy_tree(s, d, NULL, 0);
        else if (CopyFileW(s, d, TRUE)) copied++;
        free(s);
        free(d);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return copied;
}

static bool dir_missing_or_empty(const wchar_t *dir)
{
    wchar_t *pattern = path_join(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool empty = true;
    do {
        if (wcscmp(fd.cFileName, L".") && wcscmp(fd.cFileName, L"..")) empty = false;
    } while (empty && FindNextFileW(h, &fd));
    FindClose(h);
    return empty;
}

/* Copia, no mueve: las carpetas anteriores se quedan como respaldo. */
bool compat_jarvis_migrate_data(void)
{
    bool migrated = false;
    wchar_t *old_cfg = path_join(g_paths.legacy_local_dir, L"config.env");
    if (!file_exists(g_paths.config_file) && file_exists(old_cfg)) {
        /* El log y las descargas a medias de una actualización no hacen falta. */
        static const wchar_t *const SKIP[] = {COMPAT_JARVIS_LOG_NAME, L"update"};
        int n = copy_tree(g_paths.legacy_local_dir, g_paths.local_dir, SKIP, 2);
        log_msg("Configuración de la versión anterior copiada (%d archivos).", n);
        migrated = true;
    }
    free(old_cfg);
    if (dir_missing_or_empty(g_paths.memory_dir) && !dir_missing_or_empty(g_paths.legacy_memory_dir)) {
        int n = copy_tree(g_paths.legacy_memory_dir, g_paths.memory_dir, NULL, 0);
        log_msg("Memoria de la versión anterior copiada (%d archivos).", n);
        migrated = true;
    }
    return migrated;
}

const char *compat_jarvis_config_key(const char *key, char *buf, size_t n)
{
    size_t p = strlen(COMPAT_JARVIS_KEY_PREFIX);
    if (strncmp(key, COMPAT_JARVIS_KEY_PREFIX, p)) return key;
    snprintf(buf, n, "SOKARI_%s", key + p);
    return buf;
}

/* Se queda abierto mientras Sokari corre: con él, la versión anterior cree
   que ya está abierta y no arranca. */
static HANDLE g_old_mutex;

bool compat_jarvis_take_over(bool from_autostart)
{
    HANDLE m = CreateMutexW(NULL, TRUE, OLD_MUTEX);
    if (!m) return true;
    if (GetLastError() != ERROR_ALREADY_EXISTS) {
        g_old_mutex = m;
        return true;
    }
    /* Al prender la PC no se pregunta nada. */
    if (from_autostart ||
        MessageBoxW(NULL,
                    L"La versión anterior de tu asistente está abierta, y las dos no pueden correr a la vez.\n\n"
                    L"¿La cierro y abro Sokari?",
                    L"Sokari", MB_YESNO | MB_ICONQUESTION | MB_SETFOREGROUND) != IDYES) {
        CloseHandle(m);
        return false;
    }
    HWND other = FindWindowW(OLD_MSG_CLASS, NULL);
    if (other) PostMessageW(other, WM_APP_QUIT, 0, 0);
    DWORD w = WaitForSingleObject(m, 20000);
    if (w == WAIT_OBJECT_0 || w == WAIT_ABANDONED) {
        g_old_mutex = m;
        log_msg("Se cerró la versión anterior para abrir Sokari.");
        return true;
    }
    CloseHandle(m);
    MessageBoxW(NULL,
                L"La versión anterior no se cerró. Ciérrala desde su ícono de la bandeja (clic derecho > Salir) y "
                L"vuelve a abrir Sokari.",
                L"Sokari", MB_ICONWARNING);
    return false;
}

/* La entrada del registro de la versión anterior pasa a Sokari si apuntaba
   a este mismo exe, o si en este arranque se copiaron sus datos (con ellos
   se vino tu "iniciar con Windows"; si no, al prender la PC arrancaría la
   anterior). */
static void migrate_run_value(bool data_migrated)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, AUTOSTART_RUN_KEY, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &k) != ERROR_SUCCESS)
        return;
    wchar_t value[MAX_PATH * 2];
    DWORD size = (DWORD)(sizeof value - sizeof(wchar_t)), type = 0;
    if (RegQueryValueExW(k, OLD_RUN_VALUE, NULL, &type, (BYTE *)value, &size) == ERROR_SUCCESS && type == REG_SZ) {
        value[size / sizeof(wchar_t)] = 0;
        wchar_t *exe = exe_path();
        if ((data_migrated || autostart_value_points_to(value, exe)) && autostart_set(true)) {
            RegDeleteValueW(k, OLD_RUN_VALUE);
            log_msg("Inicio con Windows: ahora arranca Sokari en lugar de la versión anterior.");
        }
        free(exe);
    }
    RegCloseKey(k);
}

/* La versión en Python se ponía en el inicio con un acceso directo en la
   carpeta Inicio (que podía apuntar a python + el script). Si existe, se
   reemplaza por la entrada de Sokari, así no arrancan dos asistentes
   peleándose el micrófono. */
static void migrate_python_shortcut(void)
{
    PWSTR startup = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Startup, KF_FLAG_DEFAULT, NULL, &startup))) return;
    wchar_t *lnk = path_join(startup, OLD_NAME L".lnk");
    CoTaskMemFree(startup);
    if (file_exists(lnk)) {
        size_t n = wcslen(lnk);
        wchar_t *dbl = xcalloc(n + 2, sizeof(wchar_t));
        memcpy(dbl, lnk, n * sizeof(wchar_t));
        SHFILEOPSTRUCTW op = {.wFunc = FO_DELETE, .pFrom = dbl,
                              .fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT | FOF_NOERRORUI};
        bool recycled = autostart_set(true) && SHFileOperationW(&op) == 0 && !op.fAnyOperationsAborted;
        free(dbl);
        if (recycled) {
            log_msg("Inicio con Windows migrado del acceso directo de la versión en Python al registro.");
            AppConfig c = config_snapshot();
            c.autostart = true;
            config_apply(&c);
            config_free(&c);
        }
    }
    free(lnk);
}

void compat_jarvis_migrate_autostart(bool data_migrated)
{
    migrate_run_value(data_migrated);
    migrate_python_shortcut();
}
