/* Verifica las barreras de seguridad de las herramientas: qué rutas, apps,
   direcciones y acciones rechaza Jarvis. No usa internet ni micrófono, y no
   abre ventanas: solo llama a las herramientas con casos que deben negarse
   (y algunos normales que deben seguir funcionando). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "mesh.h"
#include "third_party/cJSON.h"
#include "tools.h"
#include "util.h"

static int g_fail, g_total;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) g_fail++;
}

/* Llama a una herramienta con argumentos "clave", "valor", ... (hasta 3 pares)
   y dice si la respuesta contiene needle. */
static bool tool_says(const char *needle, const char *tool, const char *k1, const char *v1, const char *k2,
                      const char *v2)
{
    cJSON *a = cJSON_CreateObject();
    if (k1) cJSON_AddStringToObject(a, k1, v1);
    if (k2) cJSON_AddStringToObject(a, k2, v2);
    char *args = cJSON_PrintUnformatted(a);
    cJSON_Delete(a);
    char *r = run_tool(tool, args);
    bool ok = strstr(r, needle) != NULL;
    if (!ok) printf("      %s(%s) respondió: %s\n", tool, args, r);
    free(r);
    cJSON_free(args);
    return ok;
}

static char *w2u(const wchar_t *w)
{
    return wide_to_utf8(w);
}

static void test_archivos(void)
{
    printf("-- archivos --\n");
    const char *NO = "Por seguridad no uso rutas de red";
    ensure_dir(g_paths.local_dir);
    ensure_dir(g_paths.memory_dir);
    const char secreto[] = "GROQ_API_KEY=gsk_prueba\n";
    if (!file_exists(g_paths.config_file)) write_file_atomic(g_paths.config_file, secreto, sizeof secreto - 1);

    check(path_is_off_limits(L"\\\\servidor\\compartida\\x.txt"), "ruta de red \\\\servidor\\...");
    check(path_is_off_limits(L"//servidor/compartida/x.txt"), "ruta de red //servidor/...");
    check(path_is_off_limits(L"\\\\?\\C:\\Windows"), "ruta de dispositivo \\\\?\\...");
    check(path_is_off_limits(g_paths.config_file), "config.env de Jarvis");
    wchar_t *trampa = path_join(g_paths.local_dir, L"..\\Jarvis\\.\\config.env");
    check(path_is_off_limits(trampa), "config.env con ..\\ en el camino");
    free(trampa);
    wchar_t *upper = xwcsdup(g_paths.config_file);
    CharUpperW(upper);
    check(path_is_off_limits(upper), "config.env en MAYÚSCULAS");
    free(upper);
    wchar_t *mem = path_join(g_paths.memory_dir, L"perfiles.json");
    check(path_is_off_limits(mem), "archivo de la carpeta de memoria");
    free(mem);

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t *normal = path_join(tmp, L"jarvis_prueba.txt");
    write_file_atomic(normal, "hola desde la prueba", 20);
    check(!path_is_off_limits(normal), "un archivo normal en %TEMP% sí se permite");
    char *normal_u = w2u(normal);
    check(tool_says("hola desde la prueba", "read_file", "ruta", normal_u, NULL, NULL), "read_file lee un archivo normal");

    char *local_u = w2u(g_paths.local_dir), *mem_u = w2u(g_paths.memory_dir), *cfg_u = w2u(g_paths.config_file);
    check(tool_says(NO, "read_file", "ruta", "%LOCALAPPDATA%\\Jarvis\\config.env", NULL, NULL),
          "read_file niega %LOCALAPPDATA%\\Jarvis\\config.env");
    check(tool_says(NO, "read_file", "ruta", "\\\\servidor\\c\\x.txt", NULL, NULL), "read_file niega \\\\servidor");
    check(tool_says(NO, "list_files", "carpeta", local_u, NULL, NULL), "list_files niega la carpeta de Jarvis");
    check(tool_says(NO, "buscar_archivo", "nombre", "json", "carpeta", mem_u), "buscar_archivo niega la memoria");
    check(tool_says(NO, "mover_archivo", "origen", normal_u, "destino_carpeta", local_u),
          "mover_archivo no mete archivos a la carpeta de Jarvis");
    check(file_exists(normal), "...y el archivo sigue en su lugar");
    check(tool_says(NO, "mover_archivo", "origen", normal_u, "destino_carpeta", "\\\\servidor\\c"),
          "mover_archivo no saca archivos a \\\\servidor");
    check(tool_says(NO, "borrar_archivo", "ruta", cfg_u, NULL, NULL), "borrar_archivo niega config.env");
    check(file_exists(g_paths.config_file), "...y config.env sigue ahí");

    /* Un link simbólico hacia la carpeta de Jarvis tampoco sirve de atajo
       (crear links puede pedir permisos; si no se puede, se omite). */
    wchar_t *link = path_join(tmp, L"jarvis_atajo");
    RemoveDirectoryW(link);
    if (CreateSymbolicLinkW(link, g_paths.local_dir, SYMBOLIC_LINK_FLAG_DIRECTORY | 0x2) &&
        GetFileAttributesW(link) != INVALID_FILE_ATTRIBUTES) {
        wchar_t *via = path_join(link, L"config.env");
        check(path_is_off_limits(via), "config.env a través de un link simbólico");
        free(via);
        RemoveDirectoryW(link);
    } else {
        printf("(se omite la prueba del link simbólico: no se pudo crear uno que funcione)\n");
        RemoveDirectoryW(link);
    }
    free(link);
    DeleteFileW(normal);
    free(normal);
    free(normal_u);
    free(local_u);
    free(mem_u);
    free(cfg_u);
}

static void test_open_app(void)
{
    printf("-- open_app --\n");
    const char *ESQUEMA = "solo abro apps, carpetas, archivos y páginas http";
    check(tool_says(ESQUEMA, "open_app", "name", "ms-settings:display", NULL, NULL), "niega ms-settings:");
    check(tool_says(ESQUEMA, "open_app", "name", "search-ms:query=x&crumb=location:\\\\srv\\c", NULL, NULL),
          "niega search-ms:");
    check(tool_says(ESQUEMA, "open_app", "name", "file:///C:/Windows/notepad.exe", NULL, NULL), "niega file:");
    check(tool_says(ESQUEMA, "open_app", "name", "shell:startup", NULL, NULL), "niega shell:");
    check(tool_says("no abro rutas de red", "open_app", "name", "\\\\servidor\\c\\x.exe", NULL, NULL),
          "niega \\\\servidor\\...");
    check(tool_says("no abro rutas de red", "open_app", "name", "//servidor/c/x.exe", NULL, NULL), "niega //servidor/...");

    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const wchar_t *malos[] = {L"jv_prueba.exe", L"jv_prueba.bat", L"jv_prueba.vbs", L"jv_prueba.hta", L"jv_prueba.lnk",
                              L"jv_prueba.ps1"};
    for (size_t i = 0; i < sizeof malos / sizeof *malos; i++) {
        wchar_t *f = path_join(tmp, malos[i]);
        write_file_atomic(f, "x", 1);
        char *u = w2u(f);
        char what[160];
        snprintf(what, sizeof what, "no ejecuta %s", u + strlen(u) - 13);
        check(tool_says("no abro programas ni scripts", "open_app", "name", u, NULL, NULL), what);
        char *dot = str_printf("%s.", u);
        snprintf(what, sizeof what, "ni con punto al final: %s.", u + strlen(u) - 13);
        check(tool_says("no abro programas ni scripts", "open_app", "name", dot, NULL, NULL), what);
        free(dot);
        free(u);
        DeleteFileW(f);
        free(f);
    }
    check(!open_target_is_dangerous(L"C:\\x\\notas.txt") && !open_target_is_dangerous(L"C:\\x\\tarea.pdf") &&
              !open_target_is_dangerous(L"C:\\x\\foto.jpg"),
          "txt, pdf y jpg sí se pueden abrir");
    check(open_target_is_dangerous(L"C:\\X\\SETUP.EXE") && open_target_is_dangerous(L"C:\\x\\a.txt:b.exe"),
          "EXE en mayúsculas y flujo alterno a.txt:b.exe se niegan");
    check(tool_says("No encontré", "open_app", "name", "mshta", NULL, NULL),
          "un nombre suelto que no es alias (mshta) no se ejecuta del PATH");
}

static void test_malla(void)
{
    printf("-- malla (Tailscale) --\n");
    const char *si[] = {"100.64.0.1", "100.127.255.255", "100.101.7.8", "laptop.tail1234.ts.net", "PC.TAIL9.TS.NET"};
    const char *no[] = {"100.128.0.1", "100.63.255.255", "192.168.1.5", "8.8.8.8", "256.64.0.1", "evil.com",
                        "evil.com.ts.net.evil.com", ".ts.net", "a..ts.net", "100.64.0.1.evil.com", "x@100.64.0.1",
                        "100.64.0.1:80", "localhost", ""};
    bool ok = true;
    for (size_t i = 0; i < sizeof si / sizeof *si; i++)
        if (!mesh_host_allowed(si[i])) ok = false, printf("      debió aceptar %s\n", si[i]);
    check(ok, "acepta IPs 100.64/10 y nombres *.ts.net");
    ok = true;
    for (size_t i = 0; i < sizeof no / sizeof *no; i++)
        if (mesh_host_allowed(no[i])) ok = false, printf("      debió negar %s\n", no[i]);
    check(ok, "niega todo lo demás (IPs públicas o de casa, dominios, trucos con puntos, @ y puertos)");
    check(tool_says("Solo registro direcciones de Tailscale", "registrar_dispositivo", "nombre", "atacante", "host",
                    "evil.com"),
          "registrar_dispositivo niega evil.com");
    /* Un dispositivo registrado antes de este cambio con una dirección cualquiera
       tampoco recibe el secreto. */
    wchar_t *df = path_join(g_paths.local_dir, L"dispositivos.json");
    const char viejo[] = "{\"vieja\": \"evil.com\"}";
    write_file_atomic(df, viejo, sizeof viejo - 1);
    check(tool_says("no tiene una dirección de Tailscale", "gestionar_dispositivo", "nombre", "vieja", "comando", "hola"),
          "gestionar_dispositivo no le manda el secreto a un registro viejo fuera de Tailscale");
    DeleteFileW(df);
    free(df);
}

int wmain(void)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    paths_init();
    test_archivos();
    test_open_app();
    test_malla();
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
