/* Callarla con un clic en la esfera: cuándo un clic la calla (solo si habla
   o piensa), si el clic cae en la esfera y cuándo fue clic y no arrastrar la
   esfera flotante. Sin ventanas: solo las decisiones. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "app.h"
#include "config.h"
#include "ui.h"

static int g_fail, g_total;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) g_fail++;
}

int wmain(void)
{
    SetConsoleOutputCP(CP_UTF8);
    printf("-- cuándo la calla un clic --\n");
    check(ui_click_silences(JV_SPEAKING), "mientras habla: la calla");
    check(ui_click_silences(JV_THINKING), "mientras piensa: no dice la respuesta");
    check(!ui_click_silences(JV_IDLE), "quieta: nada (no se activa sin querer)");
    check(!ui_click_silences(JV_LISTENING), "mientras te escucha: nada");

    printf("-- dónde --\n");
    check(ui_click_on_sphere(DISPLAY_FULLSCREEN, 960, 540, 1920, 1080), "pantalla completa: en el centro, sí");
    check(ui_click_on_sphere(DISPLAY_FULLSCREEN, 960 + 470, 540, 1920, 1080), "pantalla completa: en la orilla de la esfera, sí");
    check(!ui_click_on_sphere(DISPLAY_FULLSCREEN, 80, 60, 1920, 1080), "pantalla completa: en una esquina, no");
    check(!ui_click_on_sphere(DISPLAY_WINDOWED, 5, 5, 800, 600), "ventana: en la esquina, no");
    check(ui_click_on_sphere(DISPLAY_WINDOWED, 400, 300, 800, 600), "ventana: en el centro, sí");
    check(ui_click_on_sphere(DISPLAY_WINDOWED_BORDERLESS, 2, 2, 300, 300), "esfera flotante: toda la ventana es la esfera");

    printf("-- clic o arrastre --\n");
    check(ui_is_click(0, 0) && ui_is_click(3, -4), "casi sin moverse: clic");
    check(!ui_is_click(20, 15) && !ui_is_click(0, 6), "si la moviste: la arrastraste, no la callas");

    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
