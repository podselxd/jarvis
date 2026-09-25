/* La malla de punta a punta en una sola máquina: el servidor escuchando en
   127.0.0.1, Probar, una orden de verdad, "ocupado", un secreto equivocado y
   una orden que tarda (contesta «recibido» y no deja en fila a Probar).
   También lee lo que imprime Tailscale (con una advertencia antes del JSON,
   ping, «Allow incoming connections»), entiende de qué PC hablas («Chloe»,
   «mi laptop») y revisa el secreto. Todo en carpetas temporales: nunca toca
   tu configuración. */
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

static void test_tailscale_diagnostico(void)
{
    printf("-- por qué otra PC no contesta --\n");
    check(tailscale_parse_ping("pong from cloe (100.121.139.36) via 192.168.1.78:41641 in 77ms\n") == 1,
          "ping: «pong» es que Tailscale sí llega (tu salida de ayer)");
    check(tailscale_parse_ping("pong from cloe (100.121.139.36) via DERP(dfw) in 120ms\n") == 1,
          "ping: por un relevo de Tailscale también cuenta");
    check(tailscale_parse_ping("timeout waiting for ping reply\n") == 0, "ping: sin respuesta es que no llega");
    check(tailscale_parse_ping("flag provided but not defined: -until-direct\n") == -1,
          "ping: un Tailscale viejo que no entiende la opción no cuenta como «apagada»");
    check(tailscale_parse_ping(NULL) == -1, "ping: sin Tailscale, no se sabe");
    check(tailscale_parse_shields_up("{\"ControlURL\":\"x\",\"ShieldsUp\":true,\"RouteAll\":false}"),
          "debug prefs: «Allow incoming connections» apagado");
    check(!tailscale_parse_shields_up("{\"ControlURL\":\"x\",\"ShieldsUp\":false}"), "debug prefs: prendido");
    check(tailscale_parse_shields_up("{\"Node\":{\"ID\":1,\"Hostinfo\":{\"OS\":\"windows\",\"ShieldsUp\":true}}}"),
          "whois de otra PC: también se ve si lo tiene apagado");
    check(!tailscale_parse_shields_up("{\"Node\":{\"ID\":1,\"Hostinfo\":{\"OS\":\"windows\"}}}"),
          "whois sin ese dato: no se inventa");
}

static void expect_device(const char *text, const char *want)
{
    char *got = mesh_device_mentioned(text);
    bool ok = want ? got && !strcmp(got, want) : !got;
    char what[300];
    snprintf(what, sizeof what, "«%s» -> %s", text, got ? (*got ? got : "(otra PC, no se sabe cuál)") : "esta PC");
    check(ok, what);
    free(got);
}

static void test_nombres(void)
{
    printf("-- de qué PC hablas --\n");
    mesh_device_set("cloe", "100.121.139.36");
    expect_device("puedes abrir el navegador que tiene la laptop mía se llama Chloe", "cloe");
    expect_device("dile a Cloe que abra Spotify", "cloe");
    expect_device("abre Spotify en mi laptop", "cloe");
    expect_device("pon música en la otra compu", "cloe");
    expect_device("abre el navegador", NULL);
    expect_device("¿cómo está mi pc?", NULL);
    expect_device("sube el volumen de la computadora", NULL);
    expect_device("pon la otra canción", NULL);
    char *r = mesh_resolve_device("Chloe");
    check(r && !strcmp(r, "cloe"), "«Chloe» es cloe");
    free(r);
    r = mesh_resolve_device("mi laptop");
    check(r && !strcmp(r, "cloe"), "«mi laptop» es cloe (es la única)");
    free(r);
    r = mesh_resolve_device("zeus");
    check(!r, "un nombre que no se parece a ninguna: ninguna");
    free(r);

    mesh_device_set("escritorio-sala", "100.90.1.2");
    expect_device("abre Spotify en mi laptop", "");
    expect_device("dile a cloe que pause", "cloe");
    expect_device("prende la música en escritorio sala", "escritorio-sala");
    r = mesh_resolve_device("mi laptop");
    check(!r, "con dos PCs, «mi laptop» ya no se adivina");
    free(r);
    mesh_device_remove("escritorio-sala");
    mesh_device_remove("cloe");
}

static void test_secreto(void)
{
    printf("-- el secreto --\n");
    check(config_secret_problem("100.121.139.36") != NULL, "una IP no es un secreto (lo que tenía tu PC)");
    check(config_secret_problem("cloe.tail1234.ts.net") != NULL, "el nombre de una PC tampoco");
    check(config_secret_problem("hola") != NULL, "uno muy corto tampoco");
    check(config_secret_problem("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") == NULL,
          "el que genera Sokari sí");
    AppConfig c = config_snapshot();
    free(c.mesh_secret);
    c.mesh_secret = xstrdup("100.121.139.36");
    config_apply(&c);
    config_free(&c);
    char *s = config_mesh_secret(true);
    check(strlen(s) == 64 && !strchr(s, '.'), "si quedó guardada una IP, se cambia por un secreto de verdad");
    free(s);
}

static char *g_last_cmd, *g_last_origin;
static bool g_busy;
static volatile LONG g_slow_done;

static char *fake_sokari(const char *comando, const char *origen)
{
    if (!strcmp(comando, "tarda")) {
        Sleep(1500);
        InterlockedExchange(&g_slow_done, 1);
        return xstrdup("ya terminé");
    }
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

    /* Una orden larga: «recibido» enseguida, y mientras tanto Probar contesta. */
    mesh_set_ack_ms(300);
    uint64_t t0 = GetTickCount64();
    reply = NULL;
    p = mesh_send_ip("127.0.0.1", "tarda", 5000, &reply);
    uint64_t took = GetTickCount64() - t0;
    check(p == MESH_OK && reply && strstr(reply, "Recibido") && took < 1200,
          "una orden que tarda: contesta «recibido» sin esperar a que termine");
    free(reply);
    t0 = GetTickCount64();
    check(mesh_send_ip("127.0.0.1", "", 3000, NULL) == MESH_OK && GetTickCount64() - t0 < 1000,
          "mientras esa orden sigue, Probar contesta al momento (ya no espera en fila)");
    for (int i = 0; i < 40 && !InterlockedCompareExchange(&g_slow_done, 0, 0); i++) Sleep(100);
    check(InterlockedCompareExchange(&g_slow_done, 0, 0) == 1, "y la orden larga sí se termina aquí");
    mesh_set_ack_ms(5000);

    mesh_listen_stop();
    check(mesh_send_ip("127.0.0.1", "", 3000, NULL) == MESH_NO_SOKARI,
          "con el servidor apagado: «Sokari no le contesta», no «no contesta»");
}

static void test_firewall(void)
{
    printf("-- el firewall de Windows --\n");
    int fw = mesh_firewall_check();
    printf("      (esta máquina: %s)\n", fw == 1 ? "deja pasar" : fw == 0 ? "no deja pasar" : "no se pudo leer");
    check(fw >= -1 && fw <= 1 && mesh_firewall_ok() == fw, "se lee el firewall sin trabarse ni pedir permisos");
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
    wchar_t *devices = path_join(dir, L"dispositivos.json");
    DeleteFileW(devices);
    config_load();
    http_init();

    test_tailscale_json();
    test_tailscale_diagnostico();
    test_nombres();
    test_secreto();
    test_servidor();
    test_firewall();

    DeleteFileW(g_paths.config_file);
    DeleteFileW(devices);
    free(devices);
    RemoveDirectoryW(dir);
    free(dir);
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
