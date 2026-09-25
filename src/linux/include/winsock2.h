/* Sockets con los nombres de Winsock, para el código compartido que los usa
   (pruebas y malla). En Linux son los sockets de siempre. */
#ifndef SOKARI_LINUX_WINSOCK2_H
#define SOKARI_LINUX_WINSOCK2_H

#include <windows.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)
#define closesocket close
#define MAKEWORD(a, b) ((WORD)(((BYTE)(a)) | ((WORD)((BYTE)(b))) << 8))
#define SD_SEND SHUT_WR
#define SD_BOTH SHUT_RDWR

typedef struct {
    int unused;
} WSADATA;
static inline int WSAStartup(WORD version, WSADATA *data)
{
    (void)version, (void)data;
    return 0;
}
static inline int WSACleanup(void) { return 0; }

#endif
