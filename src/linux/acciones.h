/* Lo que comparten las herramientas de Linux (sistema, teclas y archivos). */
#ifndef SOKARI_LINUX_ACCIONES_H
#define SOKARI_LINUX_ACCIONES_H

#include <stdbool.h>
#include <stdint.h>

#include "linux/gnome.h"

/* Teclas de X (keysyms), como las entiende GNOME. */
#define XK_BackSpace 0xff08
#define XK_Tab 0xff09
#define XK_Return 0xff0d
#define XK_Escape 0xff1b
#define XK_Delete 0xffff
#define XK_Home 0xff50
#define XK_Left 0xff51
#define XK_Up 0xff52
#define XK_Right 0xff53
#define XK_Down 0xff54
#define XK_Prior 0xff55
#define XK_Next 0xff56
#define XK_End 0xff57
#define XK_Print 0xff61
#define XK_Insert 0xff63
#define XK_Menu 0xff67
#define XK_F1 0xffbe
#define XK_Shift_L 0xffe1
#define XK_Control_L 0xffe3
#define XK_Caps_Lock 0xffe5
#define XK_Alt_L 0xffe9
#define XK_Super_L 0xffeb

/* La tecla de Windows (VK_...) como keysym; 0 si no hay. */
uint32_t lx_keysym(unsigned short vk);

/* ¿Ahí Enter abre o ejecuta cosas? Terminales, Archivos (abre lo que esté
   seleccionado) y el propio GNOME (actividades, «Ejecutar» con Alt+F2). */
bool lx_runs_commands(const GnomeWindow *w, bool shell_ui);

/* El nombre de la app de la ventana ("Firefox", "Archivos") o, si GNOME no
   lo sabe, fallback. Nunca su título: lo pone cada página y podría traer
   instrucciones para el modelo. */
const char *lx_app_name(const GnomeWindow *w, const char *fallback);

/* Por qué no se hizo, según lo que contestó la extensión ("focus",
   "terminal"…); what: "No oprimí nada", "No escribí nada"… (heap). */
char *lx_refusal(const char *status, const char *what);

#endif
