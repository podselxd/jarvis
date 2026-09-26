#ifndef SOKARI_LINUX_LINUX_H
#define SOKARI_LINUX_LINUX_H

/* Lo propio de la versión de Linux que usan otros archivos de src/linux/ y
   las pruebas. */

#include <stdbool.h>
#include <stddef.h>

/* util_linux.c */
void sha256_bytes(const void *data, size_t n, unsigned char out[32]);

/* app_linux.c: ¿se pidió cerrar Sokari (palabra de apagado, menú)? */
bool app_quit_requested(void);

/* tts_linux.c: del catálogo de voces de Piper (voices.json), la de México que
   se va a bajar y lo que debe medir y dar su MD5. */
bool tts_pick_catalog_voice(const char *catalog_json, char **key, char **onnx_path, char **onnx_md5, long long *onnx_size,
                            char **json_path, char **json_md5, long long *json_size);

/* audio_linux.c: el volumen de la salida predeterminada, en % (puede pasar
   de 100 si alguien la subió de más) y el silencio. */
bool system_volume_get(int *percent, bool *muted);
bool system_volume_set(int percent);
bool system_mute_set(bool mute);

/* MD5 en hexadecimal (heap): el que publica el catálogo de voces de Piper. */
char *tts_md5_hex(const void *data, size_t n);
/* "es_MX-ald-medium" -> "Ald (México, Piper)" (heap). */
char *tts_piper_voice_name(const char *key);

#endif
