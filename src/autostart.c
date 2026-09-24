#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdlib.h>
#include <string.h>

#include "autostart.h"
#include "config.h"
#include "log.h"
#include "util.h"

#define RUN_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE L"Jarvis"

bool autostart_is_enabled(void)
{
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    bool on = RegQueryValueExW(k, RUN_VALUE, NULL, NULL, NULL, NULL) == ERROR_SUCCESS;
    RegCloseKey(k);
    return on;
}

bool autostart_set(bool enable)
{
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) != ERROR_SUCCESS)
        return false;
    LSTATUS rc;
    if (enable) {
        wchar_t *exe = exe_path();
        size_t n = wcslen(exe) + 3;
        wchar_t *cmd = xmalloc(sizeof(wchar_t) * n);
        swprintf(cmd, n, L"\"%ls\"", exe);
        rc = RegSetValueExW(k, RUN_VALUE, 0, REG_SZ, (const BYTE *)cmd, (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
        free(cmd);
        free(exe);
    } else {
        rc = RegDeleteValueW(k, RUN_VALUE);
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(k);
    return rc == ERROR_SUCCESS;
}

/* La versión en Python se ponía en el inicio con un acceso directo Jarvis.lnk
   en la carpeta Inicio (que podía apuntar a python + jarvis.py). Si existe,
   se reemplaza por la clave Run apuntando a este .exe, así no arrancan dos
   Jarvis distintos peleándose el micrófono. */
void autostart_migrate_legacy(void)
{
    PWSTR startup = NULL;
    if (FAILED(SHGetKnownFolderPath(&FOLDERID_Startup, KF_FLAG_DEFAULT, NULL, &startup))) return;
    wchar_t *lnk = path_join(startup, L"Jarvis.lnk");
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
            log_msg("Inicio con Windows migrado del acceso directo viejo al registro.");
            AppConfig c = config_snapshot();
            c.autostart = true;
            config_apply(&c);
            config_free(&c);
        }
    }
    free(lnk);
}
