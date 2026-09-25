/* app.h en Linux. Por ahora Sokari corre en modo texto (sokari --texto): lo
   que en Windows va a la esfera y a los subtítulos aquí se queda en el
   registro, y los avisos salen en la terminal. La interfaz gráfica llega
   después (ver ui_linux.c). */
#include <windows.h>

#include <stdio.h>

#include "app.h"
#include "log.h"

static volatile LONG g_state;
static volatile LONG g_quit;

void app_set_state(JvState s)
{
    InterlockedExchange(&g_state, (LONG)s);
}

JvState app_get_state(void)
{
    return (JvState)InterlockedCompareExchange(&g_state, 0, 0);
}

void app_set_level(float level)
{
    (void)level;
}

void app_subtitle(bool from_user, const char *text)
{
    (void)from_user, (void)text;
}

void app_status(const char *text)
{
    (void)text;
}

void app_notify(const char *title, const char *text)
{
    printf("\n[%s] %s\n", title ? title : "Sokari", text ? text : "");
    fflush(stdout);
}

bool app_is_own_window(HWND h)
{
    (void)h;
    return false;
}

void app_yield_focus(void) {}

void app_request_quit(void)
{
    InterlockedExchange(&g_quit, 1);
}

bool app_quit_requested(void)
{
    return InterlockedCompareExchange(&g_quit, 1, 1) != 0;
}
