/* Cómo arranca Sokari (Inicio, directo, primera vez), lo que se escribe en la
   clave Run, que los ajustes (modos de pantalla, salida de audio, tamaño de la
   ventana) se guardan y se leen bien, la copia de datos desde Sokari y qué exe
   baja el actualizador. Todo en carpetas temporales: nunca toca tu
   configuración, tu memoria ni el registro. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autostart.h"
#include "compat_jarvis.h"
#include "config.h"
#include "third_party/cJSON.h"
#include "update.h"
#include "util.h"

#define OLD_PREFIX COMPAT_JARVIS_KEY_PREFIX
#define OLD_LOG COMPAT_JARVIS_LOG_NAME

static int g_fail, g_total;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) g_fail++;
}

static void test_launch(void)
{
    printf("-- cómo arranca --\n");
    check(launch_kind(false, false, false) == LAUNCH_FIRST_RUN, "sin API key: primera configuración");
    check(launch_kind(false, true, true) == LAUNCH_FIRST_RUN, "sin API key, aunque arranque con Windows");
    check(launch_kind(true, false, false) == LAUNCH_HOME, "abierto a mano: ventana de Inicio");
    check(launch_kind(true, true, false) == LAUNCH_DIRECT, "con Windows: directo");
    check(launch_kind(true, false, true) == LAUNCH_DIRECT, "después de actualizarse: directo");
}

static void test_run_key(void)
{
    printf("-- clave Run --\n");
    const wchar_t *exe = L"C:\\Users\\Ana Pérez\\Sokari\\Sokari.exe";
    wchar_t *cmd = autostart_command(exe);
    check(!wcscmp(cmd, L"\"C:\\Users\\Ana Pérez\\Sokari\\Sokari.exe\" --autostart"),
          "ruta entre comillas (tiene espacios) y --autostart");
    check(!autostart_needs_refresh(cmd, exe), "la nueva no se vuelve a escribir");
    free(cmd);
    check(autostart_needs_refresh(L"\"C:\\Users\\Ana Pérez\\Sokari\\Sokari.exe\"", exe),
          "la de la versión anterior (sin --autostart) se actualiza");
    check(autostart_needs_refresh(L"\"c:\\users\\ana pérez\\sokari\\SOKARI.EXE\"  ", exe),
          "sin importar mayúsculas ni espacios al final");
    check(autostart_needs_refresh(L"C:\\Sokari\\Sokari.exe", L"C:\\Sokari\\Sokari.exe"), "también sin comillas");
    check(!autostart_needs_refresh(L"\"D:\\Otra copia\\Sokari.exe\"", exe), "si apunta a otra copia, no se toca");
    check(!autostart_needs_refresh(L"\"C:\\Users\\Ana Pérez\\Sokari\\Sokari.exe\" --otra-cosa", exe),
          "si alguien le puso otros argumentos, no se toca");
    check(!autostart_needs_refresh(L"\"C:\\Users\\Ana Pérez\\Sokari\\Sokari.exe.bak\"", exe),
          "una ruta que solo empieza igual no cuenta");
    check(!autostart_needs_refresh(NULL, exe) && !autostart_needs_refresh(L"", exe), "vacía o sin valor: nada");

    const wchar_t *j = L"C:\\Sokari\\Sokari.exe";
    check(autostart_value_points_to(L"\"C:\\Sokari\\Sokari.exe\" --autostart", j) &&
              autostart_value_points_to(L"\"c:\\sokari\\SOKARI.EXE\"", j) &&
              autostart_value_points_to(L"C:\\Sokari\\Sokari.exe --autostart", j),
          "la clave vieja apunta a este exe (con o sin comillas y argumentos)");
    check(!autostart_value_points_to(L"\"C:\\Otra\\Sokari.exe\"", j) &&
              !autostart_value_points_to(L"\"C:\\Sokari\\Sokari.exe.bak\"", j) &&
              !autostart_value_points_to(L"C:\\Sokari\\Sokari.exe2", j) && !autostart_value_points_to(NULL, j),
          "otra copia o una ruta que solo empieza igual no cuentan");
}

static void touch(const wchar_t *dir, const wchar_t *name, const char *text)
{
    ensure_dir(dir);
    wchar_t *p = path_join(dir, name);
    write_file_atomic(p, text, strlen(text));
    free(p);
}

static bool exists_in(const wchar_t *dir, const wchar_t *name)
{
    wchar_t *p = path_join(dir, name);
    bool r = GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
    free(p);
    return r;
}

static void remove_tree(const wchar_t *dir)
{
    wchar_t *pattern = path_join(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern, &fd);
    free(pattern);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L"..")) continue;
            wchar_t *p = path_join(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_tree(p);
            else DeleteFileW(p);
            free(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir);
}

static void test_migration(const wchar_t *base)
{
    printf("-- desde la versión anterior --\n");
    AppPaths saved = g_paths;
    wchar_t *root = path_join(base, L"migracion");
    remove_tree(root);
    g_paths.legacy_local_dir = path_join(root, L"Local\\Anterior");
    g_paths.local_dir = path_join(root, L"Local\\Sokari");
    g_paths.legacy_memory_dir = path_join(root, L"Escritorio\\Anterior");
    g_paths.memory_dir = path_join(root, L"Escritorio\\Sokari");
    g_paths.config_file = path_join(g_paths.local_dir, L"config.env");

    check(!compat_jarvis_migrate_data(), "instalación nueva (sin versión anterior): no copia nada");

    /* config.env de la versión anterior, con sus claves de entonces. */
    char old_cfg[128];
    snprintf(old_cfg, sizeof old_cfg, "GROQ_API_KEY=gsk_prueba\n%sUSER_NAME=Ana\n%sVOLUME=40\n", OLD_PREFIX, OLD_PREFIX);
    touch(g_paths.legacy_local_dir, L"config.env", old_cfg);
    touch(g_paths.legacy_local_dir, L"dispositivos.json", "{}");
    touch(g_paths.legacy_local_dir, OLD_LOG, "log viejo");
    wchar_t *snd = path_join(g_paths.legacy_local_dir, L"sounds");
    touch(snd, L"activacion.mp3", "mp3");
    wchar_t *upd = path_join(g_paths.legacy_local_dir, L"update");
    touch(upd, L"nuevo.exe", "MZ");
    touch(g_paths.legacy_memory_dir, L"hechos.json", "{\"ana\":[]}");
    wchar_t *sub = path_join(g_paths.legacy_memory_dir, L"Datos");
    touch(sub, L"nota.md", "hola");
    /* Como hace el arranque: la carpeta nueva ya existe (con el log) antes de copiar. */
    touch(g_paths.local_dir, L"sokari.log", "log nuevo");

    check(compat_jarvis_migrate_data(), "la primera vez copia desde las carpetas anteriores");
    wchar_t *nsnd = path_join(g_paths.local_dir, L"sounds");
    wchar_t *nsub = path_join(g_paths.memory_dir, L"Datos");
    check(exists_in(g_paths.local_dir, L"config.env") && exists_in(g_paths.local_dir, L"dispositivos.json") &&
              exists_in(nsnd, L"activacion.mp3"),
          "configuración, dispositivos y sonidos");
    check(exists_in(g_paths.memory_dir, L"hechos.json") && exists_in(nsub, L"nota.md"), "memoria, con subcarpetas");
    check(!exists_in(g_paths.local_dir, OLD_LOG) && !exists_in(g_paths.local_dir, L"update"),
          "el log viejo y las descargas de actualización no se copian");
    check(exists_in(g_paths.legacy_local_dir, L"config.env") && exists_in(g_paths.legacy_memory_dir, L"hechos.json"),
          "copia, no mueve: las carpetas anteriores quedan de respaldo");
    config_load();
    char *name = config_user_name();
    check(!strcmp(name, "Ana") && config_volume() == 40,
          "la configuración copiada se lee, con las claves de antes (tu nombre y tu volumen siguen ahí)");
    free(name);

    touch(g_paths.local_dir, L"config.env", "SOKARI_USER_NAME=Beto\n");
    check(!compat_jarvis_migrate_data(), "la segunda vez ya no copia");
    config_load();
    name = config_user_name();
    check(!strcmp(name, "Beto"), "y nunca pisa lo que cambiaste en Sokari");
    free(name);

    free(nsnd);
    free(nsub);
    free(snd);
    free(upd);
    free(sub);
    free(g_paths.legacy_local_dir);
    free(g_paths.local_dir);
    free(g_paths.legacy_memory_dir);
    free(g_paths.memory_dir);
    free(g_paths.config_file);
    remove_tree(root);
    free(root);
    g_paths = saved;
}

static cJSON *assets(const char *a, const char *b)
{
    cJSON *arr = cJSON_CreateArray();
    const char *names[] = {a, b};
    for (int i = 0; i < 2; i++) {
        if (!names[i]) continue;
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", names[i]);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

static const char *picked(cJSON *arr)
{
    const cJSON *a = update_pick_asset(arr);
    const cJSON *n = a ? cJSON_GetObjectItemCaseSensitive(a, "name") : NULL;
    return n ? n->valuestring : "(ninguno)";
}

static void test_update_asset(void)
{
    printf("-- actualizador --\n");
    cJSON *both = assets("notas.txt", "Sokari.exe"), *other = assets("Otro.exe", NULL);
    check(!strcmp(picked(both), "Sokari.exe"), "baja Sokari.exe del release");
    check(update_pick_asset(other) == NULL && update_pick_asset(NULL) == NULL, "si el release no trae Sokari.exe: nada");
    cJSON_Delete(both);
    cJSON_Delete(other);
}

static void write_config(const char *text)
{
    write_file_atomic(g_paths.config_file, text, strlen(text));
}

static void test_config(const wchar_t *dir)
{
    printf("-- configuración nueva --\n");
    write_config("");
    config_load();
    AppConfig c = config_snapshot();
    check(c.display_mode == DISPLAY_FULLSCREEN_BORDERLESS && !*c.output_name && c.win_w == -1,
          "valores por defecto: pantalla completa sin bordes, salida predeterminada, ventana sin tamaño");
    config_free(&c);

    config_set_display_mode(DISPLAY_WINDOWED);
    config_set_window_rect(-1200, 40, 800, 600);
    config_set_output("Audífonos (WH-1000XM4)");
    config_load();
    c = config_snapshot();
    check(c.display_mode == DISPLAY_WINDOWED, "modo Ventana se guarda y se vuelve a leer");
    check(c.win_x == -1200 && c.win_y == 40 && c.win_w == 800 && c.win_h == 600,
          "posición de la ventana (también en un monitor a la izquierda)");
    check(!strcmp(c.output_name, "Audífonos (WH-1000XM4)"), "salida de audio con acentos y paréntesis");
    config_free(&c);

    config_set_display_mode(DISPLAY_MINIMIZED);
    config_load();
    c = config_snapshot();
    check(c.display_mode == DISPLAY_MINIMIZED, "modo Minimizado");
    config_free(&c);
    config_set_display_mode(99);
    config_set_display_mode(-1);
    config_load();
    c = config_snapshot();
    check(c.display_mode == DISPLAY_MINIMIZED, "un modo fuera de rango no se guarda");
    config_free(&c);

    write_config("SOKARI_DISPLAY_MODE=windowed_borderless\nSOKARI_WINDOW=10,20\nSOKARI_OUTPUT=\n");
    config_load();
    c = config_snapshot();
    check(c.display_mode == DISPLAY_WINDOWED_BORDERLESS, "la esfera flotante de antes se sigue leyendo");
    check(c.win_w == -1 && c.win_h == -1, "una posición de ventana incompleta se ignora");
    config_free(&c);

    write_config("SOKARI_DISPLAY_MODE=inventado\nSOKARI_WINDOW=0,0,0,500\n");
    config_load();
    c = config_snapshot();
    check(c.display_mode == DISPLAY_FULLSCREEN_BORDERLESS, "un modo desconocido deja el de por defecto");
    check(c.win_w == -1, "un tamaño de ventana en cero se ignora");
    config_free(&c);

    check(!strcmp(display_mode_key(DISPLAY_WINDOWED), "windowed") && !strcmp(display_mode_key(DISPLAY_MINIMIZED), "minimized") &&
              !strcmp(display_mode_key(42), "fullscreen_borderless"),
          "nombres de los modos en config.env");
}

int wmain(void)
{
    SetConsoleOutputCP(CP_UTF8);
    paths_init();
    /* Todo en una carpeta temporal: nunca se toca tu config.env. */
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t *dir = path_join(tmp, L"sokari_test_inicio");
    ensure_dir(dir);
    free(g_paths.local_dir);
    g_paths.local_dir = xwcsdup(dir);
    free(g_paths.config_file);
    g_paths.config_file = path_join(dir, L"config.env");

    test_launch();
    test_run_key();
    test_config(dir);
    test_migration(dir);
    test_update_asset();

    DeleteFileW(g_paths.config_file);
    RemoveDirectoryW(dir);
    free(dir);
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
