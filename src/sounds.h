#ifndef JARVIS_SOUNDS_H
#define JARVIS_SOUNDS_H

/* Sonidos: si pusiste tus propios activacion.mp3/busqueda.mp3 (en
   %LOCALAPPDATA%\Jarvis\sounds, o elegidos desde Configuración) se usan esos;
   si no, un tono corto generado por Jarvis para la activación. */
void sound_activation(void);
void sound_search_start(void);
void sound_search_stop(void);

#endif
