/* Cómo arranca Jarvis (Inicio, directo, primera vez), lo que se escribe en la
   clave Run y que los ajustes nuevos (modos de pantalla, salida de audio,
   tamaño de la ventana) se guardan y se leen bien. Usa un config.env
   temporal: nunca toca tu configuración ni el registro. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "autostart.h"
#include "config.h"
#include "util.h"

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
    const wchar_t *exe = L"C:\\Users\\Ana Pérez\\Jarvis\\Jarvis.exe";
    wchar_t *cmd = autostart_command(exe);
    check(!wcscmp(cmd, L"\"C:\\Users\\Ana Pérez\\Jarvis\\Jarvis.exe\" --autostart"),
          "ruta entre comillas (tiene espacios) y --autostart");
    check(!autostart_needs_refresh(cmd, exe), "la nueva no se vuelve a escribir");
    free(cmd);
    check(autostart_needs_refresh(L"\"C:\\Users\\Ana Pérez\\Jarvis\\Jarvis.exe\"", exe),
          "la de la versión anterior (sin --autostart) se actualiza");
    check(autostart_needs_refresh(L"\"c:\\users\\ana pérez\\jarvis\\JARVIS.EXE\"  ", exe),
          "sin importar mayúsculas ni espacios al final");
    check(autostart_needs_refresh(L"C:\\Jarvis\\Jarvis.exe", L"C:\\Jarvis\\Jarvis.exe"), "también sin comillas");
    check(!autostart_needs_refresh(L"\"D:\\Otra copia\\Jarvis.exe\"", exe), "si apunta a otra copia, no se toca");
    check(!autostart_needs_refresh(L"\"C:\\Users\\Ana Pérez\\Jarvis\\Jarvis.exe\" --otra-cosa", exe),
          "si alguien le puso otros argumentos, no se toca");
    check(!autostart_needs_refresh(L"\"C:\\Users\\Ana Pérez\\Jarvis\\Jarvis.exe.bak\"", exe),
          "una ruta que solo empieza igual no cuenta");
    check(!autostart_needs_refresh(NULL, exe) && !autostart_needs_refresh(L"", exe), "vacía o sin valor: nada");
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

    write_config("JARVIS_DISPLAY_MODE=windowed_borderless\nJARVIS_WINDOW=10,20\nJARVIS_OUTPUT=\n");
    config_load();
    c = config_snapshot();
    check(c.display_mode == DISPLAY_WINDOWED_BORDERLESS, "la esfera flotante de antes se sigue leyendo");
    check(c.win_w == -1 && c.win_h == -1, "una posición de ventana incompleta se ignora");
    config_free(&c);

    write_config("JARVIS_DISPLAY_MODE=inventado\nJARVIS_WINDOW=0,0,0,500\n");
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
    wchar_t *dir = path_join(tmp, L"jarvis_test_inicio");
    ensure_dir(dir);
    free(g_paths.local_dir);
    g_paths.local_dir = xwcsdup(dir);
    free(g_paths.config_file);
    g_paths.config_file = path_join(dir, L"config.env");

    test_launch();
    test_run_key();
    test_config(dir);

    DeleteFileW(g_paths.config_file);
    RemoveDirectoryW(dir);
    free(dir);
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
