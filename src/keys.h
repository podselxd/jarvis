#ifndef SOKARI_KEYS_H
#define SOKARI_KEYS_H

#include <stdbool.h>

/* Una tecla o combinación dicha en español o en inglés: "control k",
   "ctrl+shift+esc", "alt tab", "windows d", "flecha abajo", "F5" o "efe
   cinco", "control zeta". Sin efectos: se puede probar sin tocar el
   teclado. */
#define KEYS_MAX 6
typedef struct {
    unsigned short vk[KEYS_MAX]; /* códigos de tecla de Windows, primero los modificadores */
    int n;
    bool has_delete; /* Supr: en el Explorador borra, así que cuenta como borrar */
    bool has_enter;  /* Enter: en un chat, manda */
} KeyCombo;

bool keys_parse(const char *spec, KeyCombo *out);
/* ¿Una tecla con Ctrl, Alt, Shift o Windows ("control zeta", "alt tab")? */
bool keys_is_combo(const KeyCombo *k);
/* ¿Solo Ctrl o Shift? Oprimidos solos no hacen nada. */
bool keys_does_nothing(const KeyCombo *k);
/* "Ctrl+Shift+Esc" (heap), para decir qué se oprimió. */
char *keys_describe(const KeyCombo *k);

/* Ir a la pestaña cuyo título diga want: pide el título y, si no es, pasa a
   la siguiente (Ctrl+Tab), hasta max veces. Devuelve cuántas pasó (0 si ya
   estaba), o -1 si dio la vuelta sin encontrarla o si title devolvió NULL
   (ya no se puede seguir). Recibe las dos acciones para poder probarla sin
   navegador. */
typedef char *(*TabTitleFn)(void *ctx); /* heap */
typedef void (*TabNextFn)(void *ctx);
int keys_find_tab(const char *want, TabTitleFn title, TabNextFn next, void *ctx, int max);

/* Los atajos conocidos de una app ("chrome", "discord", "word", "windows"…),
   para que el modelo sepa qué oprimir. NULL si no la conoce. Estático. */
const char *keys_shortcuts_for(const char *app);

#endif
