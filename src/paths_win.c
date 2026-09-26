/* Dónde guarda Sokari sus cosas en Windows: %LOCALAPPDATA%\Sokari para la
   configuración y el registro, y el Escritorio (o el de OneDrive) para la
   memoria, como desde la versión en Python. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <stdlib.h>

#include "config.h"
#include "util.h"

static wchar_t *known_folder(REFKNOWNFOLDERID id)
{
    PWSTR p = NULL;
    wchar_t *r = NULL;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, NULL, &p))) r = xwcsdup(p);
    CoTaskMemFree(p);
    return r;
}

/* Carpetas de la versión anterior. Se quedaron como respaldo con copia de tu
   API key y tu memoria: solo sirven para que las herramientas de archivos
   tampoco las toquen. */
#define OLD_DIR_NAME L"Jarvis"

void paths_init(void)
{
    wchar_t *local = known_folder(&FOLDERID_LocalAppData);
    if (!local) local = expand_env(L"%USERPROFILE%\\AppData\\Local");
    g_paths.local_dir = path_join(local, L"Sokari");
    g_paths.legacy_local_dir = path_join(local, OLD_DIR_NAME);
    free(local);

    /* Misma regla que desde la versión en Python: %OneDrive%\Desktop\Sokari,
       o ~\Desktop\Sokari. */
    wchar_t base[MAX_PATH * 2];
    DWORD n = GetEnvironmentVariableW(L"OneDrive", base, (DWORD)(sizeof base / sizeof base[0]));
    wchar_t *root = (n && n < sizeof base / sizeof base[0]) ? xwcsdup(base) : NULL;
    if (!root) root = known_folder(&FOLDERID_Profile);
    if (!root) root = expand_env(L"%USERPROFILE%");
    wchar_t *desk = path_join(root, L"Desktop");
    g_paths.memory_dir = path_join(desk, L"Sokari");
    g_paths.legacy_memory_dir = path_join(desk, OLD_DIR_NAME);
    free(desk);
    free(root);

    g_paths.config_file = path_join(g_paths.local_dir, L"config.env");
    g_paths.log_file = path_join(g_paths.local_dir, L"sokari.log");
    g_paths.sounds_dir = path_join(g_paths.local_dir, L"sounds");
    g_paths.update_dir = path_join(g_paths.local_dir, L"update");
    ensure_dir(g_paths.local_dir);
}
