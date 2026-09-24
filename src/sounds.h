#ifndef JARVIS_SOUNDS_H
#define JARVIS_SOUNDS_H

/* Sonidos: si pusiste tus propios activacion.mp3/busqueda.mp3 (en
   %LOCALAPPDATA%\Sokari\sounds, o elegidos desde Configuración) se usan esos;
   si no, un tono corto generado por Sokari para la activación. */
void sound_activation(void);
/* Siempre el tono integrado (sale por la salida de audio elegida; tus MP3
   personalizados van por la predeterminada de Windows). */
void sound_chime(void);
void sound_search_start(void);
void sound_search_stop(void);

#endif
