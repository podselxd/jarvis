/* Lo de la malla que cambia con el sistema: dónde está Tailscale y cómo se
   corre, su IP en esta PC, y el firewall. Windows: mesh_win.c; Linux:
   src/linux/mesh_linux.c. Todo lo demás (el servidor, probar, mandar,
   «Revisar la malla») es mesh.c, igual en los dos. */
#ifndef SOKARI_MESH_OS_H
#define SOKARI_MESH_OS_H

#include "util.h"

#define MESH_PORT 8765

/* Dónde está tailscale (heap, UTF-8) o NULL si no está instalado. */
char *mesh_tailscale_path(void);
/* Corre tailscale con esos argumentos (sin shell) y devuelve lo que imprime
   (heap; NULL si no está o no arrancó). Lo que imprime como error va aparte
   en *err: mezclado, cualquier advertencia de Tailscale rompía su JSON. Si
   tarda más de timeout_ms, lo termina. */
char *mesh_run_tailscale(const char *const args[], int timeout_ms, long *exit_code, char **err);
/* Al empezar a escuchar: revisa si el firewall deja entrar las órdenes (en
   Windows, si no, pide permiso una sola vez). */
void mesh_firewall_on_listen(void);
/* Los renglones del firewall en «Revisar la malla». */
void mesh_diagnose_firewall(StrBuf *r);

#endif
