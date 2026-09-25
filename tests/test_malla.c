/* La malla de punta a punta en una sola máquina: el servidor escuchando en
   127.0.0.1, Probar, una orden de verdad, "ocupado" y un secreto equivocado.
   También lee lo que imprime Tailscale aunque venga con una advertencia antes
   del JSON. Todo en carpetas temporales: nunca toca tu configuración. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "http.h"
#include "mesh.h"
#include "util.h"

static int g_fail, g_total;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) g_fail++;
}

static void test_tailscale_json(void)
{
    printf("-- lo que imprime Tailscale --\n");
    const char *status =
        "Warning: client version \"1.90.1\" != tailscaled server version \"1.90.2\"\n"
        "{\"Version\":\"1.90.2\",\"BackendState\":\"Running\","
        "\"Self\":{\"HostName\":\"ESCRITORIO\",\"OS\":\"windows\",\"UserID\":7102938475611234,"
        "\"TailscaleIPs\":[\"100.124.55.72\"]},"
        "\"Peer\":{\"nodekey:a\":{\"HostName\":\"LAPTOP-Ismael\",\"OS\":\"windows\",\"UserID\":7102938475611234,"
        "\"Online\":true,\"TailscaleIPs\":[\"100.121.139.36\"]},"
        "\"nodekey:b\":{\"HostName\":\"Pixel 8\",\"OS\":\"android\",\"Online\":false,\"TailscaleIPs\":[\"100.90.1.2\"]}},"
        "\"User\":{\"99\":{\"ID\":99,\"LoginName\":\"otra@gmail.com\"},"
        "\"7102938475611234\":{\"ID\":7102938475611234,\"LoginName\":\"ismael@gmail.com\",\"DisplayName\":\"Ismael\"}}}";
    TailscaleStatus st;
    bool ok = tailscale_parse_status(status, &st);
    check(ok && !strcmp(st.state, "Running") && st.account && !strcmp(st.account, "ismael@gmail.com") && st.peers == 2,
          "con una advertencia antes del JSON: conectado, tu cuenta y cuántos dispositivos ve");
    tailscale_status_free(&st);

    MeshDevice *peers;
    int n = tailscale_parse_peers(status, &peers);
    check(n == 1 && !strcmp(peers[0].name, "laptop-ismael") && !strcmp(peers[0].host, "100.121.139.36"),
          "Detectar también lee ese JSON: solo la PC con Windows");
    mesh_devices_free(peers, n);

    ok = tailscale_parse_status("{\"BackendState\":\"NeedsLogin\",\"Self\":{},\"User\":null}", &st);
    check(ok && !strcmp(st.state, "NeedsLogin") && !st.account && st.peers == 0, "sin entrar a Tailscale: sin cuenta");
    tailscale_status_free(&st);
    check(!tailscale_parse_status("failed to connect to local tailscaled; it doesn't appear to be running", &st) &&
              !st.state,
          "si Tailscale no corre, no inventa nada");
    check(!tailscale_parse_status(NULL, &st), "sin salida, tampoco");

    char *who = tailscale_parse_whois_account(
        "{\"Node\":{\"ID\":1,\"Name\":\"laptop-ismael.tail1234.ts.net.\",\"User\":7102938475611234},"
        "\"UserProfile\":{\"ID\":7102938475611234,\"LoginName\":\"ismael@gmail.com\",\"DisplayName\":\"Ismael\"}}");
    check(who && !strcmp(who, "ismael@gmail.com"), "whois: de quién es esa PC");
    free(who);
    who = tailscale_parse_whois_account("no peer found with that IP");
    check(!who, "whois de una IP desconocida: de nadie");
    free(who);
}

static char *g_last_cmd, *g_last_origin;
static bool g_busy;

static char *fake_sokari(const char *comando, const char *origen)
{
    free(g_last_cmd);
    free(g_last_origin);
    g_last_cmd = xstrdup(comando);
    g_last_origin = xstrdup(origen);
    return g_busy ? NULL : str_printf("hecho: %s", comando);
}

static int raw_post(const char *headers, const char *body)
{
    HttpRequest req = {.method = "POST",
                       .url = "http://127.0.0.1:8765/comando",
                       .headers = headers,
                       .body = body,
                       .body_len = strlen(body),
                       .timeout_ms = 5000};
    HttpResponse r = http_request(&req);
    int status = r.status;
    http_response_free(&r);
    return status;
}

static void test_servidor(void)
{
    printf("-- el servidor de punta a punta (127.0.0.1) --\n");
    char *secret = config_mesh_secret(true);
    check(secret && strlen(secret) == 64, "hay un secreto de 32 bytes");
    free(secret);
    if (!mesh_listen_at("127.0.0.1", fake_sokari)) {
        check(false, "el servidor escucha en 127.0.0.1:8765");
        return;
    }
    check(true, "el servidor escucha en 127.0.0.1:8765");

    check(mesh_send_ip("127.0.0.1", "", 5000, NULL) == MESH_OK && !g_last_cmd,
          "Probar: la orden vacía contesta y allá no se ejecuta nada");

    char *reply = NULL;
    MeshProbe p = mesh_send_ip("127.0.0.1", "abre spotify y pon música", 5000, &reply);
    check(p == MESH_OK && reply && !strcmp(reply, "hecho: abre spotify y pon música"),
          "una orden llega completa (con acentos) y regresa la respuesta");
    check(g_last_origin && !strcmp(g_last_origin, "127.0.0.1"), "allá se sabe desde dónde llegó");
    free(reply);

    g_busy = true;
    check(mesh_send_ip("127.0.0.1", "otra cosa", 5000, NULL) == MESH_BUSY, "si allá está ocupado, lo dice");
    g_busy = false;

    free(g_last_cmd);
    g_last_cmd = NULL;
    int st = raw_post("Content-Type: application/json\r\nX-Sokari-Secret: equivocado\r\n", "{\"comando\":\"borra todo\"}");
    check(st == 401 && !g_last_cmd,
          "con otro secreto (y sin la misma cuenta de Tailscale) se rechaza y no se ejecuta nada");
    st = raw_post("Content-Type: application/json\r\n", "{\"comando\":\"borra todo\"}");
    check(st == 401 && !g_last_cmd, "sin secreto, igual");

    mesh_listen_stop();
    check(mesh_send_ip("127.0.0.1", "", 3000, NULL) == MESH_NO_SOKARI,
          "con el servidor apagado: «Sokari no le contesta», no «no contesta»");
}

int wmain(void)
{
    SetConsoleOutputCP(CP_UTF8);
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    paths_init();
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    wchar_t *dir = path_join(tmp, L"sokari_test_malla");
    ensure_dir(dir);
    free(g_paths.local_dir);
    g_paths.local_dir = xwcsdup(dir);
    free(g_paths.config_file);
    g_paths.config_file = path_join(dir, L"config.env");
    DeleteFileW(g_paths.config_file);
    config_load();
    http_init();

    test_tailscale_json();
    test_servidor();

    DeleteFileW(g_paths.config_file);
    RemoveDirectoryW(dir);
    free(dir);
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
