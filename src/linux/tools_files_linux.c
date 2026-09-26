/* Archivos en Linux: tus carpetas (Descargas, Documentos… como las tengas
   en tu idioma), listar, leer, buscar, mover y mandar a la papelera. Las
   mismas reglas que en Windows: nunca toca las carpetas donde Sokari guarda
   su configuración y su memoria (ni por un enlace), ni rutas de red; lee
   solo archivos normales (nunca se queda esperando en uno especial); y nunca
   borra para siempre. */
#include <windows.h>

#include <gio/gio.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "tools.h"
#include "util.h"

#define MAX_LIST_ENTRIES 60
#define MAX_SEARCH_HITS 20
#define MAX_FILES_SCANNED 20000
#define MAX_FILE_READ_CHARS 6000

typedef struct {
    const char *alias;
    GUserDirectory dir;
    const char *fallback; /* si tu sistema no la tiene configurada */
} FolderAlias;

static const FolderAlias FOLDERS[] = {
    {"descargas", G_USER_DIRECTORY_DOWNLOAD, "Descargas"},  {"downloads", G_USER_DIRECTORY_DOWNLOAD, "Downloads"},
    {"escritorio", G_USER_DIRECTORY_DESKTOP, "Escritorio"}, {"desktop", G_USER_DIRECTORY_DESKTOP, "Desktop"},
    {"documentos", G_USER_DIRECTORY_DOCUMENTS, "Documentos"}, {"documents", G_USER_DIRECTORY_DOCUMENTS, "Documents"},
    {"imagenes", G_USER_DIRECTORY_PICTURES, "Imágenes"},    {"imágenes", G_USER_DIRECTORY_PICTURES, "Imágenes"},
    {"fotos", G_USER_DIRECTORY_PICTURES, "Imágenes"},       {"musica", G_USER_DIRECTORY_MUSIC, "Música"},
    {"música", G_USER_DIRECTORY_MUSIC, "Música"},           {"videos", G_USER_DIRECTORY_VIDEOS, "Vídeos"},
    {"vídeos", G_USER_DIRECTORY_VIDEOS, "Vídeos"},
};

static const char *SEARCH_ALIASES[] = {"descargas", "escritorio", "documentos"};

static const char *home_dir(void)
{
    const char *h = getenv("HOME");
    return h && *h ? h : g_get_home_dir();
}

/* Las carpetas de XDG (~/.config/user-dirs.dirs): en español se llaman
   Descargas, Documentos…, y cada quien puede tenerlas en otro lado. */
wchar_t *known_folder_alias(const char *alias)
{
    char *low = str_lower(alias);
    char *t = str_trim(low);
    free(low);
    wchar_t *r = NULL;
    for (size_t i = 0; i < sizeof FOLDERS / sizeof *FOLDERS && !r; i++) {
        if (strcmp(t, FOLDERS[i].alias)) continue;
        const char *d = g_get_user_special_dir(FOLDERS[i].dir);
        char *fb = g_build_filename(home_dir(), FOLDERS[i].fallback, NULL);
        /* Si la de XDG no está configurada (o es tu carpeta personal), la de siempre. */
        const char *use = d && strcmp(d, home_dir()) && g_file_test(d, G_FILE_TEST_IS_DIR) ? d : fb;
        r = utf8_to_wide(use);
        g_free(fb);
    }
    free(t);
    return r;
}

wchar_t *resolve_path(const char *ruta)
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

/* ------------------------------------------------ carpetas prohibidas --- */

static bool path_inside(const char *path, const char *dir)
{
    if (!dir || !*dir) return false;
    size_t n = strlen(dir);
    while (n > 1 && dir[n - 1] == '/') n--;
    return !strncmp(path, dir, n) && (!path[n] || path[n] == '/');
}

/* La ruta real (enlaces resueltos) de lo que existe; de lo que no existe
   todavía, la de su carpeta más cercana que sí, más el resto. */
static char *final_path(const char *full)
{
    char *r = realpath(full, NULL);
    if (r) return r;
    char *parent = g_path_get_dirname(full);
    char *rest = g_path_get_basename(full);
    char *out = NULL;
    if (strcmp(parent, full) && strcmp(parent, ".")) {
        char *fp = final_path(parent);
        out = g_build_filename(fp, rest, NULL);
        free(fp);
    }
    g_free(parent);
    g_free(rest);
    if (!out) return xstrdup(full);
    char *copy = xstrdup(out);
    g_free(out);
    return copy;
}

static bool is_data_dir(const char *path, bool resolve)
{
    const wchar_t *dirs[] = {g_paths.local_dir, g_paths.memory_dir, g_paths.legacy_local_dir, g_paths.legacy_memory_dir};
    bool in = false;
    for (size_t i = 0; i < sizeof dirs / sizeof *dirs && !in; i++) {
        if (!dirs[i]) continue;
        char *d = wide_to_utf8(dirs[i]);
        in = path_inside(path, d);
        if (!in && resolve) {
            char *f = final_path(d);
            in = path_inside(path, f);
            free(f);
        }
        free(d);
    }
    return in;
}

static bool is_network(const char *p)
{
    if ((p[0] == '/' || p[0] == '\\') && (p[1] == '/' || p[1] == '\\')) return true;
    if (strstr(p, "://")) return true; /* smb://, sftp://… */
    char gvfs[64];
    snprintf(gvfs, sizeof gvfs, "/run/user/%u/gvfs", (unsigned)getuid());
    return path_inside(p, gvfs);
}

/* /proc, /sys y /dev no son archivos tuyos: son lo que corre en la PC (y ahí
   hay "archivos" que nunca terminan de leerse). */
static bool is_system_fs(const char *p)
{
    return path_inside(p, "/proc") || path_inside(p, "/sys") || path_inside(p, "/dev");
}

static char *absolute(const char *p)
{
    char *a = g_canonicalize_filename(p, NULL);
    char *r = xstrdup(a);
    g_free(a);
    return r;
}

bool path_is_off_limits(const wchar_t *path)
{
    char *p = wide_to_utf8(path);
    bool off = is_network(p);
    if (!off) {
        char *full = absolute(p);
        char *fin = final_path(full);
        off = is_data_dir(fin, true) || is_data_dir(full, false) || is_system_fs(full) || is_system_fs(fin) ||
              is_network(fin);
        free(fin);
        free(full);
    }
    free(p);
    return off;
}

static const char OFF_LIMITS[] =
    "Por seguridad no uso rutas de red ni las carpetas donde Sokari guarda su configuración y su memoria (ni /proc, "
    "/sys o /dev).";

/* ------------------------------------------------------------ listar --- */

static int cmp_names(const void *a, const void *b)
{
    return _wcsicmp(*(wchar_t *const *)a + 1, *(wchar_t *const *)b + 1);
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
    char *p = wide_to_utf8(path);
    DIR *d = opendir(p);
    if (!d) {
        free(p);
        free(path);
        return str_printf("No pude leer '%s'.", carpeta);
    }
    int cap = 64, n = 0;
    wchar_t **names = xmalloc(sizeof(wchar_t *) * (size_t)cap);
    struct dirent *e;
    while ((e = readdir(d))) {
        /* Los que empiezan con punto están ocultos en Linux (como en Archivos). */
        if (e->d_name[0] == '.') continue;
        char *full = g_build_filename(p, e->d_name, NULL);
        struct stat st;
        bool is_dir = !stat(full, &st) && S_ISDIR(st.st_mode);
        g_free(full);
        if (n == cap) {
            cap *= 2;
            names = xrealloc(names, sizeof(wchar_t *) * (size_t)cap);
        }
        wchar_t *w = utf8_to_wide(e->d_name);
        size_t len = wcslen(w);
        names[n] = xmalloc(sizeof(wchar_t) * (len + 2));
        names[n][0] = is_dir ? L'D' : L'F';
        memcpy(names[n] + 1, w, sizeof(wchar_t) * (len + 1));
        free(w);
        n++;
    }
    closedir(d);
    free(p);
    free(path);
    if (!n) {
        free(names);
        return str_printf("'%s' está vacía.", carpeta);
    }
    qsort(names, (size_t)n, sizeof *names, cmp_names);
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < n && i < MAX_LIST_ENTRIES; i++) {
        char *u = wide_to_utf8(names[i] + 1);
        sb_appendf(&sb, "%s[%s] %s", i ? "\n" : "", names[i][0] == L'D' ? "carpeta" : "archivo", u);
        free(u);
    }
    if (n > MAX_LIST_ENTRIES) sb_appendf(&sb, "\n... y %d más", n - MAX_LIST_ENTRIES);
    for (int i = 0; i < n; i++) free(names[i]);
    free(names);
    return sb_steal(&sb);
}

/* ------------------------------------------------------------- leer --- */

/* cut: el archivo sigue después de lo leído, así que la última letra puede
   haber quedado a la mitad. */
static bool valid_utf8(const unsigned char *s, size_t n, bool cut)
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
        if (i + extra >= n) return cut; /* cortada al final de lo leído: vale si el archivo sigue */
        for (size_t k = 1; k <= extra; k++)
            if ((s[i + k] & 0xC0) != 0x80) return false;
        i += extra + 1;
    }
    return true;
}

/* UTF-16 (lo que guarda el Bloc de notas de Windows con "Unicode") a UTF-8. */
static char *utf16le_to_utf8(const unsigned char *b, size_t n)
{
    StrBuf sb;
    sb_init(&sb);
    for (size_t i = 0; i + 1 < n; i += 2) {
        uint32_t c = (uint32_t)b[i] | ((uint32_t)b[i + 1] << 8);
        if (c >= 0xD800 && c <= 0xDBFF && i + 3 < n) {
            uint32_t lo = (uint32_t)b[i + 2] | ((uint32_t)b[i + 3] << 8);
            if (lo >= 0xDC00 && lo <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
                i += 2;
            } else {
                c = 0xFFFD;
            }
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;
        }
        char out[4];
        int k = 0;
        if (c < 0x80) out[k++] = (char)c;
        else if (c < 0x800) out[k++] = (char)(0xC0 | (c >> 6)), out[k++] = (char)(0x80 | (c & 0x3F));
        else if (c < 0x10000)
            out[k++] = (char)(0xE0 | (c >> 12)), out[k++] = (char)(0x80 | ((c >> 6) & 0x3F)),
            out[k++] = (char)(0x80 | (c & 0x3F));
        else
            out[k++] = (char)(0xF0 | (c >> 18)), out[k++] = (char)(0x80 | ((c >> 12) & 0x3F)),
            out[k++] = (char)(0x80 | ((c >> 6) & 0x3F)), out[k++] = (char)(0x80 | (c & 0x3F));
        sb_append_n(&sb, out, (size_t)k);
    }
    char *r = sb_steal(&sb);
    return r ? r : xstrdup("");
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
    char *p = wide_to_utf8(path);
    free(path);
    /* Sin bloquearse: una "tubería" o un dispositivo nunca terminan de leerse. */
    int fd = open(p, O_RDONLY | O_NONBLOCK | O_CLOEXEC | O_NOCTTY);
    free(p);
    struct stat st;
    if (fd < 0) return str_printf("No pude leer '%s'.", ruta);
    if (fstat(fd, &st) || !S_ISREG(st.st_mode)) {
        close(fd);
        return str_printf("'%s' no es un archivo de texto legible.", ruta);
    }
    size_t cap = MAX_FILE_READ_CHARS * 4 + 16;
    unsigned char *buf = xmalloc(cap + 2);
    size_t got = 0;
    while (got < cap) {
        ssize_t k = read(fd, buf + got, cap - got);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        got += (size_t)k;
    }
    close(fd);
    buf[got] = buf[got + 1] = 0;

    char *text = NULL;
    if (got >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) {
        text = utf16le_to_utf8(buf + 2, got - 2);
    } else {
        size_t start = (got >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) ? 3 : 0;
        for (size_t i = start; i < got && i < 4096; i++) {
            if (buf[i] == 0) {
                free(buf);
                return str_printf("'%s' no es un archivo de texto legible (parece binario).", ruta);
            }
        }
        if (valid_utf8(buf + start, got - start, (unsigned long long)st.st_size > got)) text = xstrndup((char *)buf + start, got - start);
        else text = cp1252_to_utf8((char *)buf + start, got - start);
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
    bool truncated = text[i] != 0 || (unsigned long long)st.st_size > (unsigned long long)got;
    text[i] = 0;
    if (truncated) {
        char *r = str_printf("%s\n[...se cortó aquí, el archivo sigue...]", text);
        free(text);
        return r;
    }
    return text;
}

/* ------------------------------------------------------------ buscar --- */

static const char *SKIP_DIRS[] = {"node_modules", "__pycache__", "snap"};

typedef struct {
    const char *needle; /* en minúsculas */
    StrBuf *hits;
    int nhits;
    int scanned;
} SearchCtx;

static void search_dir(const char *dir, SearchCtx *c, int depth)
{
    if (depth > 24 || c->nhits >= MAX_SEARCH_HITS || c->scanned >= MAX_FILES_SCANNED) return;
    DIR *d = opendir(dir);
    if (!d) return;
    int ndirs = 0, cap = 16;
    char **dirs = xmalloc(sizeof(char *) * (size_t)cap);
    struct dirent *e;
    while (c->nhits < MAX_SEARCH_HITS && c->scanned < MAX_FILES_SCANNED && (e = readdir(d))) {
        /* Ocultas (.git, .cache, .local…) fuera, como en Archivos. */
        if (e->d_name[0] == '.') continue;
        char *full = g_build_filename(dir, e->d_name, NULL);
        struct stat st;
        /* lstat: un enlace a una carpeta no se sigue (podría dar vueltas o
           salirse a donde no es). */
        if (lstat(full, &st)) {
            g_free(full);
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            bool skip = false;
            for (size_t i = 0; i < sizeof SKIP_DIRS / sizeof *SKIP_DIRS; i++)
                if (!strcmp(e->d_name, SKIP_DIRS[i])) skip = true;
            if (skip || is_data_dir(full, false) || is_system_fs(full)) {
                g_free(full);
                continue;
            }
            if (ndirs == cap) {
                cap *= 2;
                dirs = xrealloc(dirs, sizeof(char *) * (size_t)cap);
            }
            dirs[ndirs++] = xstrdup(full);
            g_free(full);
            continue;
        }
        c->scanned++;
        char *lower = str_lower(e->d_name);
        if (strstr(lower, c->needle)) {
            sb_appendf(c->hits, "%s%s", c->hits->len ? "\n" : "", full);
            c->nhits++;
        }
        free(lower);
        g_free(full);
    }
    closedir(d);
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
    char *needle = str_lower(nombre);
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
        char *u = wide_to_utf8(root);
        search_dir(u, &c, 0);
        sb_append(&where, u);
        free(u);
        free(root);
    } else {
        for (size_t i = 0; i < sizeof SEARCH_ALIASES / sizeof *SEARCH_ALIASES; i++) {
            wchar_t *root = known_folder_alias(SEARCH_ALIASES[i]);
            char *u = root ? wide_to_utf8(root) : NULL;
            if (u) search_dir(u, &c, 0);
            free(u);
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

/* ------------------------------------------------------------- mover --- */

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
        char *ub = wide_to_utf8(base), *us = wide_to_utf8(src), *ud = wide_to_utf8(dst);
        struct stat st;
        if (!lstat(ud, &st)) {
            r = str_printf("Ya hay un archivo llamado '%s' en '%s', no lo piso.", ub, destino);
        } else {
            /* Sin reemplazar: si mientras tanto apareció uno con ese nombre, falla. */
            GFile *from = g_file_new_for_path(us), *to = g_file_new_for_path(ud);
            GError *err = NULL;
            if (g_file_move(from, to, G_FILE_COPY_NOFOLLOW_SYMLINKS | G_FILE_COPY_ALL_METADATA, NULL, NULL, NULL, &err))
                r = str_printf("Listo, moví %s a %s.", ub, destino);
            else
                r = str_printf("No pude mover el archivo (%s).", err ? err->message : "error desconocido");
            g_clear_error(&err);
            g_object_unref(from);
            g_object_unref(to);
        }
        free(ub);
        free(us);
        free(ud);
        free(dst);
    }
    free(src);
    free(dst_dir);
    return r;
}

/* ----------------------------------------------------------- borrar --- */

/* El sistema y tus carpetas principales enteras (Documentos, Descargas, tu
   carpeta personal…), ni la raíz de una memoria USB. */
static bool is_protected_path(const char *full)
{
    if (!strcmp(full, "/")) return true;
    static const char *const SYSTEM[] = {"/bin", "/boot", "/dev",  "/etc", "/lib", "/lib32", "/lib64", "/libx32",
                                         "/opt", "/proc", "/root", "/sbin", "/snap", "/srv", "/sys",  "/usr",
                                         "/var"};
    for (size_t i = 0; i < sizeof SYSTEM / sizeof *SYSTEM; i++)
        if (path_inside(full, SYSTEM[i])) return true;
    if (path_inside(full, "/run") && !path_inside(full, "/run/media")) return true;
    static const char *const ROOTS[] = {"/home", "/media", "/mnt", "/tmp", "/run/media"};
    for (size_t i = 0; i < sizeof ROOTS / sizeof *ROOTS; i++)
        if (!strcmp(full, ROOTS[i])) return true;
    const char *home = home_dir();
    if (!strcmp(full, home)) return true;
    const char *homes[] = {".config", ".local", ".local/share", ".cache"};
    for (size_t i = 0; i < sizeof homes / sizeof *homes; i++) {
        char *h = g_build_filename(home, homes[i], NULL);
        bool same = !strcmp(full, h);
        g_free(h);
        if (same) return true;
    }
    for (int d = 0; d < G_USER_N_DIRECTORIES; d++) {
        const char *u = g_get_user_special_dir((GUserDirectory)d);
        if (u && !strcmp(full, u)) return true;
    }
    for (size_t i = 0; i < sizeof FOLDERS / sizeof *FOLDERS; i++) {
        char *fb = g_build_filename(home, FOLDERS[i].fallback, NULL);
        bool same = !strcmp(full, fb);
        g_free(fb);
        if (same) return true;
    }
    /* Donde empieza otro disco (una USB, otra partición): como la raíz de una unidad. */
    struct stat me, up;
    char *parent = g_path_get_dirname(full);
    bool mount = !lstat(full, &me) && S_ISDIR(me.st_mode) && !stat(parent, &up) && me.st_dev != up.st_dev;
    g_free(parent);
    return mount;
}

/* Nunca borra para siempre: manda a la Papelera. Si ahí no hay papelera (una
   carpeta de red, algunos discos), no lo borra y lo dice. */
char *tool_borrar_archivo(const cJSON *a)
{
    const char *ruta = arg_str(a, "ruta");
    wchar_t *path = resolve_path(ruta);
    if (path_is_off_limits(path)) {
        free(path);
        return xstrdup(OFF_LIMITS);
    }
    char *p = wide_to_utf8(path);
    char *full = absolute(p);
    struct stat st;
    char *r;
    if (lstat(full, &st)) {
        r = str_printf("No encontré '%s'.", ruta);
    } else {
        char *real = final_path(full);
        if (is_protected_path(full) || is_protected_path(real)) {
            r = xstrdup("Por seguridad no mando a la papelera carpetas del sistema ni carpetas principales enteras "
                        "(como Documentos o Descargas completas).");
        } else {
            GFile *f = g_file_new_for_path(full);
            GError *err = NULL;
            char *base = g_path_get_basename(full);
            if (g_file_trash(f, NULL, &err))
                r = str_printf("Listo, mandé '%s' a la papelera — se puede recuperar desde ahí.", base);
            else if (err && err->code == G_IO_ERROR_NOT_SUPPORTED)
                r = str_printf("No mandé '%s' a la papelera: ahí no hay papelera, y para siempre no lo borro.", base);
            else
                r = str_printf("No pude mandar '%s' a la papelera.", base);
            g_clear_error(&err);
            g_free(base);
            g_object_unref(f);
        }
        free(real);
    }
    free(full);
    free(p);
    free(path);
    return r;
}
