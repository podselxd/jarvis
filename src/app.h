#ifndef SOKARI_APP_H
#define SOKARI_APP_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdbool.h>

typedef enum {
    JV_IDLE = 0,
    JV_LISTENING,
    JV_THINKING,
    JV_SPEAKING,
} JvState;

/* Puente entre el hilo de voz / herramientas y la interfaz. Todas son seguras
   de llamar desde cualquier hilo (encolan mensajes a la ventana principal) y
   no hacen nada si la interfaz no está abierta (por ejemplo en los tests). */
void app_set_state(JvState s);
JvState app_get_state(void);
void app_set_level(float level);
void app_subtitle(bool from_user, const char *text);
void app_status(const char *text);
void app_notify(const char *title, const char *text);
bool app_is_own_window(HWND h);
void app_yield_focus(void);
void app_request_quit(void);

#endif
