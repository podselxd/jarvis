#ifndef SOKARI_MESH_H
#define SOKARI_MESH_H

#include <stdbool.h>

/* Procesa un comando de texto que llegó por la malla. Lo implementa el
   agente; origen es el nombre del dispositivo que la mandó (o su IP si no
   está registrado). Devuelve la respuesta (heap) o NULL si Sokari está
   ocupado. */
typedef char *(*MeshHandler)(const char *comando, const char *origen);

/* Arranca un hilo que levanta el servidor en cuanto Tailscale está conectado
   (aunque se conecte después de abrir Sokari) y lo mueve si cambia de IP. */
bool mesh_start(MeshHandler handler);
void mesh_stop(void);
char *mesh_tailscale_ip(void);
/* IP donde esta PC está recibiendo órdenes ahora, o NULL. */
char *mesh_listening_ip(void);
bool tailscale_installed(void);
bool mesh_host_allowed(const char *host);

/* Tus dispositivos registrados (dispositivos.json): nombre -> dirección de
   Tailscale. */
typedef struct {
    char *name;
    char *host;
} MeshDevice;
int mesh_devices(MeshDevice **out);
void mesh_devices_free(MeshDevice *list, int n);
bool mesh_device_set(const char *name, const char *host); /* false si no es de Tailscale */
bool mesh_device_remove(const char *name);
char *mesh_device_name_for_ip(const char *ip);

/* Prueba un dispositivo con una orden vacía: dice si contesta y si el secreto
   coincide, sin que allá se haga nada. */
typedef enum {
    MESH_OK,
    MESH_BUSY,
    MESH_BAD_SECRET,
    MESH_NO_SOKARI, /* la PC está pero nadie escucha en el puerto: Sokari cerrado o sin su servidor */
    MESH_NO_ANSWER, /* apagada, sin Tailscale o bloqueada por el firewall */
    MESH_NOT_FOUND,
    MESH_BAD_HOST,
    MESH_ERROR,
} MeshProbe;
MeshProbe mesh_probe(const char *host);
const char *mesh_probe_text(MeshProbe p);

/* Las otras PCs con Windows de tu red de Tailscale (nombre y IP), leídas de
   "tailscale status --json". Si no encuentra ninguna, *why (heap, si no es
   NULL) dice por qué, en palabras para el usuario. */
int tailscale_windows_peers(MeshDevice **out, char **why);
/* La parte que lee ese JSON (aparte para poder probarla sin Tailscale). */
int tailscale_parse_peers(const char *json, MeshDevice **out);

/* Lo que "tailscale status --json" dice de esta PC. */
typedef struct {
    char *state;   /* "Running" si está conectado */
    char *account; /* la cuenta con la que entraste (LoginName), o NULL */
    int peers;     /* otros dispositivos que ve tu red */
} TailscaleStatus;
bool tailscale_parse_status(const char *out, TailscaleStatus *st);
void tailscale_status_free(TailscaleStatus *st);
/* De "tailscale whois --json <ip>": la cuenta dueña de esa IP (heap) o NULL. */
char *tailscale_parse_whois_account(const char *out);

/* Revisa la malla paso a paso (Tailscale, tu cuenta, el servidor, el
   firewall, lo que ve tu red y cada PC registrada) y devuelve el reporte
   (heap), con ✓ o ✗ en cada renglón. Tarda unos segundos. */
char *mesh_diagnose(void);

/* Para las pruebas: escuchar en una IP cualquiera sin esperar a Tailscale, y
   mandar una orden a una IP ya revisada (*reply con la respuesta si hubo). */
bool mesh_listen_at(const char *ip, MeshHandler handler);
void mesh_listen_stop(void);
MeshProbe mesh_send_ip(const char *ip, const char *comando, int timeout_ms, char **reply);
/* Permite el puerto de la malla en el firewall de Windows solo para tu red de
   Tailscale (pide permiso de administrador). */
bool mesh_allow_firewall(void);

#endif
