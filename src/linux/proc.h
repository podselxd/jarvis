#ifndef SOKARI_LINUX_PROC_H
#define SOKARI_LINUX_PROC_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* Programas externos sin shell: los argumentos van tal cual (nunca se arma un
   comando con texto), sin heredar descriptores y con la entrada, la salida y
   los errores que se pidan (NULL: a /dev/null). */
pid_t proc_spawn(const char *const argv[], int *in_fd, int *out_fd, int *err_fd, const char *const env_extra[]);

/* Corre y espera: manda input (puede ser NULL) a su entrada, junta su salida
   (hasta max_out bytes, heap) y su código de salida (-1 si no arrancó, se
   pasó de timeout_ms y se mató, o terminó por una señal). */
char *proc_run(const char *const argv[], const void *input, size_t input_len, int timeout_ms, size_t max_out,
               size_t *out_len, int *exit_code);

/* La ruta completa de un programa del PATH, o NULL (heap). */
char *proc_which(const char *name);

/* Lee una línea (sin el \n) antes de timeout_ms. NULL si no llegó a tiempo o
   se cerró (heap). */
char *proc_read_line(int fd, int timeout_ms);

bool proc_write_all(int fd, const void *data, size_t len);

/* Espera a que termine (hasta timeout_ms) y si no, lo mata. */
int proc_finish(pid_t pid, int timeout_ms);

#endif
