#ifndef SOKARI_INTENTS_H
#define SOKARI_INTENTS_H

#include <stdbool.h>

/* Comandos simples que se resuelven en tu PC, sin preguntarle al modelo:
   play/pausa, canción siguiente/anterior, volumen, la ventana de enfrente,
   abrir una app, el Explorador o tus carpetas, y "gracias". Son instantáneos,
   no gastan cupo de Groq y no se equivocan. Lo que no sea claramente uno de
   estos (o traiga algo más, como "pon la canción de AC/DC") va al modelo. */

typedef enum {
    IN_PLAY,
    IN_PAUSE,
    IN_NEXT,
    IN_PREV,
    IN_VOL_UP,
    IN_VOL_DOWN,
    IN_VOL_SET, /* arg: el nivel */
    IN_MUTE,
    IN_MINIMIZE,
    IN_MINIMIZE_ALL,
    IN_MAXIMIZE,
    IN_FULLSCREEN,
    IN_CLOSE_TAB,
    IN_CLOSE_WINDOW,
    IN_EXPLORER,
    IN_FOLDER,   /* arg: "descargas", "documentos"... */
    IN_OPEN_APP, /* arg: el nombre de la app */
    IN_THANKS,
} IntentKind;

typedef struct {
    IntentKind kind;
    char arg[48];
    int pos; /* dónde lo dijiste: se hacen en ese orden */
} IntentItem;

typedef struct {
    IntentItem items[4];
    int n;
    bool greeting; /* venía con "¿cómo andas?" o parecido */
} IntentList;

/* ¿El texto es solo comandos simples (con saludos y relleno alrededor)?
   Sin efectos: se puede probar sin tocar la PC. */
bool intents_parse(const char *text, IntentList *out);

/* Los ejecuta con las herramientas de siempre y devuelve qué decir (heap).
   *handled = false si hay que dejárselo al modelo (por ejemplo, no existe una
   app con ese nombre). */
char *intents_run(const IntentList *l, bool *handled);

/* ¿Esta palabra es "Sokari" mal transcrito (Zachary, Akari, Sotori, Zucari…)?
   Recibe una palabra en minúsculas y sin acentos. */
bool intents_is_name_word(const char *w);

/* Minúsculas, sin acentos ni signos: " hola que onda sokari ", con un espacio
   al principio y al final para buscar palabras completas. */
char *intents_normalize(const char *text);

/* El mismo texto con tu nombre bien escrito: "Hey, Zachary" -> "Hey, Sokari".
   Lo demás (acentos, signos) queda igual. */
char *intents_fix_name(const char *text);

#endif
