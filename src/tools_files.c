#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "tools.h"
#include "util.h"

#define MAX_LIST_ENTRIES 60
#define MAX_SEARCH_HITS 20
#define MAX_FILES_SCANNED 20000
#define MAX_FILE_READ_CHARS 6000

typedef struct {
    const char *alias;
    const KNOWNFOLDERID *id;
} FolderAlias;

static const FolderAlias FOLDERS[] = {
    {"descargas", &FOLDERID_Downloads}, {"downloads", &FOLDERID_Downloads},  {"escritorio", &FOLDERID_Desktop},
    {"desktop", &FOLDERID_Desktop},     {"documentos", &FOLDERID_Documents}, {"documents", &FOLDERID_Documents},
    {"imagenes", &FOLDERID_Pictures},   {"imágenes", &FOLDERID_Pictures},   {"fotos", &FOLDERID_Pictures},
    {"musica", &FOLDERID_Music},        {"música", &FOLDERID_Music},        {"videos", &FOLDERID_Videos},
    {"vídeos", &FOLDERID_Videos},
};

static const char *SEARCH_ALIASES[] = {"descargas", "escritorio", "documentos"};

/* Carpetas como Escritorio/Documentos pueden estar redirigidas a OneDrive;
   SHGetKnownFolderPath devuelve la ubicación real en esta máquina. */
wchar_t *known_folder_alias(const char *alias)
{
    char *low = str_lower(alias);
    char *t = str_trim(low);
    free(low);
    wchar_t *r = NULL;
    for (size_t i = 0; i < sizeof FOLDERS / sizeof *FOLDERS && !r; i++) {
        if (strcmp(t, FOLDERS[i].alias)) continue;
        PWSTR p = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERS[i].id, KF_FLAG_DEFAULT, NULL, &p))) r = xwcsdup(p);
        CoTaskMemFree(p);
    }
    free(t);
    return r;
}

static wchar_t *resolve_path(const char *ruta)
{
    wchar_t *k = known_folder_alias(ruta);
    if (k) return k;
    char *t = str_trim(ruta);
    size_t n = strlen(t);
    if (n >= 2 && ((t[0] == '"' && t[n - 1] == '"') || (t[0] == '\'' && t[n - 1] == '\''))) {
        memmove(t, t + 1, n - 2);
        t[n - 2] = 0;
    }
    wchar_t *w = utf8_to_wide(t);
    free(t);
    wchar_t *e = expand_env(w);
    free(w);
    return e;
}

static wchar_t *absolute_path(const wchar_t *path)
{
    DWORD n = GetFullPathNameW(path, 0, NULL, NULL);
    if (!n) return xwcsdup(path);
    wchar_t *buf = xmalloc(sizeof(wchar_t) * n);
    DWORD got = GetFullPathNameW(path, n, buf, NULL);
    if (!got || got >= n) {
        free(buf);
        return xwcsdup(path);
    }
    return buf;
}

/* La ruta real de algo que existe, con links, junctions y nombres cortos 8.3
   resueltos; si no se puede abrir, la misma ruta que llegó. */
static wchar_t *final_path(const wchar_t *full)
{
    HANDLE h = CreateFileW(full, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                           OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) return xwcsdup(full);
    wchar_t *r = NULL;
    DWORD n = GetFinalPathNameByHandleW(h, NULL, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n) {
        r = xmalloc(sizeof(wchar_t) * (n + 1));
        DWORD got = GetFinalPathNameByHandleW(h, r, n + 1, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        if (!got || got > n) {
            free(r);
            r = NULL;
        }
    }
    CloseHandle(h);
    if (!r) return xwcsdup(full);
    if (!wcsncmp(r, L"\\\\?\\UNC\\", 8)) memmove(r + 2, r + 8, (wcslen(r + 8) + 1) * sizeof(wchar_t));
    else if (!wcsncmp(r, L"\\\\?\\", 4)) memmove(r, r + 4, (wcslen(r + 4) + 1) * sizeof(wchar_t));
    return r;
}

static bool path_inside(const wchar_t *path, const wchar_t *dir)
{
    if (!dir) return false;
    size_t n = wcslen(dir);
    while (n && (dir[n - 1] == L'\\' || dir[n - 1] == L'/')) n--;
    return n && !_wcsnicmp(path, dir, n) && (!path[n] || path[n] == L'\\' || path[n] == L'/');
}

/* Ninguna herramienta de archivos toca rutas de red (\\servidor\...: con solo
   preguntar si existen, Windows le manda a ese servidor tu usuario y el hash
   de tu contraseña) ni las carpetas de datos de Sokari, donde están la API key,
   la palabra de apagado, el secreto de la malla, la memoria y los perfiles:
   nada de eso tiene que poder terminar en la conversación. Tampoco las de
   la versión anterior, que tienen una copia de lo mismo. */
static bool is_data_dir(const wchar_t *path, bool resolve)
{
    const wchar_t *dirs[] = {g_paths.local_dir, g_paths.memory_dir, g_paths.legacy_local_dir, g_paths.legacy_memory_dir};
    bool in = false;
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs && !in; i++) {
        if (!dirs[i]) continue;
        in = path_inside(path, dirs[i]);
        if (!in && resolve) {
            wchar_t *d = final_path(dirs[i]);
            in = path_inside(path, d);
            free(d);
        }
    }
    return in;
}

bool path_is_off_limits(const wchar_t *path)
{
    wchar_t *full = absolute_path(path);
    bool off = (full[0] == L'\\' || full[0] == L'/') && (full[1] == L'\\' || full[1] == L'/');
    if (!off) {
        wchar_t *fin = final_path(full);
        off = is_data_dir(fin, true) || is_data_dir(full, false);
        free(fin);
    }
    free(full);
    return off;
}

static const char OFF_LIMITS[] =
    "Por seguridad no uso rutas de red ni las carpetas donde Sokari guarda su configuración y su memoria.";

static int cmp_names(const void *a, const void *b)
{
    return _wcsicmp(*(wchar_t *const *)a, *(wchar_t *const *)b);
}

char *tool_list_files(const cJSON *a)
{
    const char *carpeta = arg_str(a, "carpeta");
    wchar_t *path = resolve_path(carpeta);
    if (path_is_off_limits(path)) {
        free(path);
        return xstrdup(OFF_LIMITS);
    }
    if (!dir_exists(path)) {
        free(path);
        return str_printf("No encontré la carpeta '%s'.", carpeta);
    }
    wchar_t *pattern = path_join(path, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) {
        free(path);
        return str_printf("No pude leer '%s'.", carpeta);
    }
    int cap = 64, n = 0;
    wchar_t **names = xmalloc(sizeof(wchar_t *) * (size_t)cap);
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (n == cap) {
            cap *= 2;
            names = xrealloc(names, sizeof(wchar_t *) * (size_t)cap);
        }
        size_t len = wcslen(fd.cFileName);
        names[n] = xmalloc(sizeof(wchar_t) * (len + 2));
        names[n][0] = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L'D' : L'F';
        memcpy(names[n] + 1, fd.cFileName, sizeof(wchar_t) * (len + 1));
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    free(path);
    if (!n) {
        free(names);
        return str_printf("'%s' está vacía.", carpeta);
    }
    for (int i = 0; i < n; i++) names[i]++;
    qsort(names, (size_t)n, sizeof *names, cmp_names);
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < n; i++) {
        if (i < MAX_LIST_ENTRIES) {
            char *u = wide_to_utf8(names[i]);
            sb_appendf(&sb, "%s[%s] %s", i ? "\n" : "", names[i][-1] == L'D' ? "carpeta" : "archivo", u);
            free(u);
        }
    }
    if (n > MAX_LIST_ENTRIES) sb_appendf(&sb, "\n... y %d más", n - MAX_LIST_ENTRIES);
    for (int i = 0; i < n; i++) free(names[i] - 1);
    free(names);
    return sb_steal(&sb);
}

static bool valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        size_t extra;
        if (c < 0x80) extra = 0;
        else if ((c & 0xE0) == 0xC0) extra = 1;
        else if ((c & 0xF0) == 0xE0) extra = 2;
        else if ((c & 0xF8) == 0xF0) extra = 3;
        else return false;
        if (i + extra >= n) return true; /* secuencia cortada al final de lo leído: vale */
        for (size_t k = 1; k <= extra; k++)
            if ((s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

char *tool_read_file(const cJSON *a)
{
    const char *ruta = arg_str(a, "ruta");
    wchar_t *path = resolve_path(ruta);
    if (path_is_off_limits(path)) {
        free(path);
        return xstrdup(OFF_LIMITS);
    }
    if (!file_exists(path)) {
        free(path);
        return str_printf("No encontré el archivo '%s'.", ruta);
    }
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    free(path);
    if (h == INVALID_HANDLE_VALUE) return str_printf("No pude leer '%s'.", ruta);
    size_t cap = MAX_FILE_READ_CHARS * 4 + 16;
    unsigned char *buf = xmalloc(cap + 2);
    DWORD got = 0;
    ReadFile(h, buf, (DWORD)cap, &got, NULL);
    LARGE_INTEGER size;
    GetFileSizeEx(h, &size);
    CloseHandle(h);
    buf[got] = buf[got + 1] = 0;

    char *text = NULL;
    if (got >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
        text = wide_n_to_utf8((wchar_t *)(buf + 2), (int)((got - 2) / 2));
    } else {
        size_t start = (got >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) ? 3 : 0;
        for (size_t i = start; i < got && i < 4096; i++) {
            if (buf[i] == 0) {
                free(buf);
                return str_printf("'%s' no es un archivo de texto legible (parece binario).", ruta);
            }
        }
        if (valid_utf8(buf + start, got - start)) {
            text = xstrndup((char *)buf + start, got - start);
        } else {
            int wn = MultiByteToWideChar(1252, 0, (char *)buf + start, (int)(got - start), NULL, 0);
            wchar_t *w = xmalloc(sizeof(wchar_t) * (size_t)(wn + 1));
            MultiByteToWideChar(1252, 0, (char *)buf + start, (int)(got - start), w, wn);
            text = wide_n_to_utf8(w, wn);
            free(w);
        }
    }
    free(buf);
    if (str_is_blank(text)) {
        free(text);
        return str_printf("'%s' está vacío o no es texto legible.", ruta);
    }
    size_t chars = 0, i = 0;
    while (text[i] && chars < MAX_FILE_READ_CHARS) {
        i++;
        while (((unsigned char)text[i] & 0xC0) == 0x80) i++;
        chars++;
    }
    bool truncated = text[i] != 0 || (unsigned long long)size.QuadPart > (unsigned long long)got;
    text[i] = 0;
    if (truncated) {
        char *r = str_printf("%s\n[...se cortó aquí, el archivo sigue...]", text);
        free(text);
        return r;
    }
    return text;
}

static const wchar_t *SKIP_DIRS[] = {L".git", L"node_modules", L"__pycache__", L"$RECYCLE.BIN", L"AppData"};

typedef struct {
    const wchar_t *needle;
    StrBuf *hits;
    int nhits;
    int scanned;
} SearchCtx;

static void search_dir(const wchar_t *dir, SearchCtx *c, int depth)
{
    if (depth > 24 || c->nhits >= MAX_SEARCH_HITS || c->scanned >= MAX_FILES_SCANNED) return;
    wchar_t *pattern = path_join(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(pattern, FindExInfoBasic, &fd, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    free(pattern);
    if (h == INVALID_HANDLE_VALUE) return;
    int ndirs = 0, cap = 16;
    wchar_t **dirs = xmalloc(sizeof(wchar_t *) * (size_t)cap);
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
            bool skip = false;
            for (size_t i = 0; i < sizeof SKIP_DIRS / sizeof *SKIP_DIRS; i++)
                if (!_wcsicmp(fd.cFileName, SKIP_DIRS[i])) skip = true;
            wchar_t *sub = path_join(dir, fd.cFileName);
            if (skip || is_data_dir(sub, false)) {
                free(sub);
                continue;
            }
            if (ndirs == cap) {
                cap *= 2;
                dirs = xrealloc(dirs, sizeof(wchar_t *) * (size_t)cap);
            }
            dirs[ndirs++] = sub;
            continue;
        }
        c->scanned++;
        wchar_t lower[MAX_PATH];
        wcsncpy(lower, fd.cFileName, MAX_PATH - 1);
        lower[MAX_PATH - 1] = 0;
        CharLowerW(lower);
        if (wcsstr(lower, c->needle)) {
            wchar_t *full = path_join(dir, fd.cFileName);
            char *u = wide_to_utf8(full);
            sb_appendf(c->hits, "%s%s", c->hits->len ? "\n" : "", u);
            free(u);
            free(full);
            c->nhits++;
        }
    } while (c->nhits < MAX_SEARCH_HITS && c->scanned < MAX_FILES_SCANNED && FindNextFileW(h, &fd));
    FindClose(h);
    for (int i = 0; i < ndirs; i++) {
        search_dir(dirs[i], c, depth + 1);
        free(dirs[i]);
    }
    free(dirs);
}

char *tool_buscar_archivo(const cJSON *a)
{
    char *nombre = str_trim(arg_str(a, "nombre"));
    const char *carpeta = arg_str(a, "carpeta");
    if (!*nombre) {
        free(nombre);
        return xstrdup("No dijiste qué archivo buscar.");
    }
    wchar_t *needle = utf8_to_wide(nombre);
    CharLowerW(needle);
    StrBuf hits;
    sb_init(&hits);
    SearchCtx c = {.needle = needle, .hits = &hits};
    StrBuf where;
    sb_init(&where);
    if (!str_is_blank(carpeta)) {
        wchar_t *root = resolve_path(carpeta);
        if (path_is_off_limits(root)) {
            free(root);
            free(needle);
            free(nombre);
            sb_free(&hits);
            sb_free(&where);
            return xstrdup(OFF_LIMITS);
        }
        search_dir(root, &c, 0);
        char *u = wide_to_utf8(root);
        sb_append(&where, u);
        free(u);
        free(root);
    } else {
        for (size_t i = 0; i < sizeof SEARCH_ALIASES / sizeof *SEARCH_ALIASES; i++) {
            wchar_t *root = known_folder_alias(SEARCH_ALIASES[i]);
            if (root) search_dir(root, &c, 0);
            free(root);
        }
        sb_append(&where, "Descargas, Escritorio o Documentos");
    }
    char *r;
    if (!c.nhits) {
        r = str_printf("No encontré ningún archivo con '%s' en %s.", nombre, where.data);
        sb_free(&hits);
    } else {
        r = sb_steal(&hits);
    }
    sb_free(&where);
    free(needle);
    free(nombre);
    return r;
}

char *tool_mover_archivo(const cJSON *a)
{
    const char *origen = arg_str(a, "origen");
    const char *destino = arg_str(a, "destino_carpeta");
    wchar_t *src = resolve_path(origen);
    wchar_t *dst_dir = resolve_path(destino);
    char *r;
    if (path_is_off_limits(src) || path_is_off_limits(dst_dir)) {
        r = xstrdup(OFF_LIMITS);
    } else if (!file_exists(src)) {
        r = str_printf("No encontré el archivo '%s'.", origen);
    } else if (!dir_exists(dst_dir)) {
        r = str_printf("No encontré la carpeta destino '%s'.", destino);
    } else {
        const wchar_t *base = path_basename(src);
        wchar_t *dst = path_join(dst_dir, base);
        char *ub = wide_to_utf8(base);
        if (GetFileAttributesW(dst) != INVALID_FILE_ATTRIBUTES)
            r = str_printf("Ya hay un archivo llamado '%s' en '%s', no lo piso.", ub, destino);
        else if (MoveFileExW(src, dst, MOVEFILE_COPY_ALLOWED))
            r = str_printf("Listo, moví %s a %s.", ub, destino);
        else
            r = str_printf("No pude mover el archivo (error %lu).", (unsigned long)GetLastError());
        free(ub);
        free(dst);
    }
    free(src);
    free(dst_dir);
    return r;
}

static bool is_protected_path(const wchar_t *path)
{
    wchar_t full[MAX_PATH * 2];
    DWORD got = GetFullPathNameW(path, MAX_PATH * 2, full, NULL);
    if (!got || got >= MAX_PATH * 2) return true; /* si no cupo, full quedó sin llenar */
    size_t n = wcslen(full);
    while (n > 3 && (full[n - 1] == L'\\' || full[n - 1] == L'/')) full[--n] = 0;
    if (n <= 3) return true; /* raíz de una unidad */
    static const KNOWNFOLDERID *roots[] = {&FOLDERID_Windows, &FOLDERID_ProgramFiles, &FOLDERID_ProgramFilesX86,
                                           &FOLDERID_Profile, &FOLDERID_Desktop,      &FOLDERID_Documents,
                                           &FOLDERID_Downloads, &FOLDERID_Pictures,   &FOLDERID_Music,
                                           &FOLDERID_Videos,  &FOLDERID_LocalAppData, &FOLDERID_RoamingAppData};
    for (size_t i = 0; i < sizeof roots / sizeof *roots; i++) {
        PWSTR p = NULL;
        if (SUCCEEDED(SHGetKnownFolderPath(roots[i], KF_FLAG_DEFAULT, NULL, &p))) {
            bool same = !_wcsicmp(p, full);
            size_t pl = wcslen(p);
            bool inside_system = (roots[i] == &FOLDERID_Windows || roots[i] == &FOLDERID_ProgramFiles ||
                                  roots[i] == &FOLDERID_ProgramFilesX86) &&
                                 !_wcsnicmp(p, full, pl) && (full[pl] == L'\\' || !full[pl]);
            CoTaskMemFree(p);
            if (same || inside_system) return true;
        }
    }
    return false;
}

/* Nunca borra para siempre: manda a la Papelera. Si Windows no pudiera
   reciclarlo (unidad sin papelera, archivo enorme), FOF_WANTNUKEWARNING hace
   que el propio Windows pregunte antes, en vez de borrarlo en silencio. */
char *tool_borrar_archivo(const cJSON *a)
{
    const char *ruta = arg_str(a, "ruta");
    wchar_t *path = resolve_path(ruta);
    if (path_is_off_limits(path)) {
        free(path);
        return xstrdup(OFF_LIMITS);
    }
    if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES) {
        free(path);
        return str_printf("No encontré '%s'.", ruta);
    }
    if (is_protected_path(path)) {
        free(path);
        return xstrdup("Por seguridad no mando a la papelera carpetas del sistema ni carpetas principales enteras "
                       "(como Documentos o Descargas completas).");
    }
    size_t n = wcslen(path);
    wchar_t *dbl = xcalloc(n + 2, sizeof(wchar_t));
    memcpy(dbl, path, n * sizeof(wchar_t));
    SHFILEOPSTRUCTW op = {0};
    op.wFunc = FO_DELETE;
    op.pFrom = dbl;
    op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_WANTNUKEWARNING | FOF_SILENT;
    int rc = SHFileOperationW(&op);
    char *base = wide_to_utf8(path_basename(path));
    char *r;
    if (rc == 0 && !op.fAnyOperationsAborted)
        r = str_printf("Listo, mandé '%s' a la papelera de reciclaje — se puede recuperar desde ahí.", base);
    else
        r = str_printf("No pude mandar '%s' a la papelera.", base);
    free(base);
    free(dbl);
    free(path);
    return r;
}
