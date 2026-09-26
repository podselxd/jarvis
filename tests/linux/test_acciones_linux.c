/* Las acciones de Linux que no necesitan GNOME: abrir apps (con apps de
   mentira), qué archivos no abre, tus carpetas, leer, buscar, mover y mandar
   a la papelera (nunca borrar para siempre), las carpetas prohibidas, el
   volumen, la música (un reproductor de mentira por D-Bus), el estado de la
   PC y qué dice cuando falta GNOME o su extensión. Todo en una carpeta
   personal temporal y con un D-Bus propio: nunca toca tu sesión. Lo que usa
   la extensión de GNOME de verdad está en tests/linux/gnome/. */
#include <windows.h>

#include <gio/gio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "config.h"
#include "keys.h"
#include "linux/acciones.h"
#include "linux/gnome.h"
#include "linux/linux.h"
#include "tools.h"
#include "util.h"

static int g_fail, g_total;
static char g_home[256];
static GTestDBus *g_bus;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    fflush(stdout);
    if (!ok) g_fail++;
}

static char *tool(const char *name, const char *fmt, ...) __attribute__((format(gnu_printf, 2, 3)));
static char *tool(const char *name, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    StrBuf sb;
    sb_init(&sb);
    sb_vappendf(&sb, fmt, ap);
    va_end(ap);
    char *r = run_tool(name, sb.data);
    sb_free(&sb);
    return r;
}

static bool starts(char *r, const char *start)
{
    bool ok = str_starts_with(r, start);
    if (!ok) printf("      contestó: %s\n", r);
    free(r);
    return ok;
}

static bool has(char *r, const char *needle)
{
    bool ok = strstr(r, needle) != NULL;
    if (!ok) printf("      contestó: %s\n", r);
    free(r);
    return ok;
}

static char *at(const char *rel)
{
    return str_printf("%s/%s", g_home, rel);
}

static void put(const char *rel, const void *data, size_t n)
{
    char *p = at(rel);
    char *d = g_path_get_dirname(p);
    g_mkdir_with_parents(d, 0700);
    g_free(d);
    FILE *f = fopen(p, "wb");
    if (f) {
        fwrite(data, 1, n, f);
        fclose(f);
    }
    free(p);
}

static bool exists(const char *rel)
{
    char *p = at(rel);
    struct stat st;
    bool e = !lstat(p, &st);
    free(p);
    return e;
}

static bool wait_exists(const char *rel, int ms)
{
    for (int t = 0; t < ms; t += 50) {
        if (exists(rel)) return true;
        Sleep(50);
    }
    return exists(rel);
}

/* --------------------------------------------- lo que prepara todo --- */

static void desktop(const char *id, const char *name, const char *extra)
{
    char *rel = str_printf("share/applications/%s", id);
    char *text = str_printf("[Desktop Entry]\nType=Application\nName=%s\nExec=/usr/bin/touch \"%s/%s.abierta\"\n%s", name,
                            g_home, name, extra ? extra : "");
    put(rel, text, strlen(text));
    free(text);
    free(rel);
}

static void setup(void)
{
    snprintf(g_home, sizeof g_home, "/tmp/sokari-acciones-XXXXXX");
    if (!mkdtemp(g_home)) exit(2);
    setenv("HOME", g_home, 1);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_CACHE_HOME");
    char *share = at("share");
    setenv("XDG_DATA_DIRS", share, 1);
    free(share);
    setenv("XDG_CURRENT_DESKTOP", "GNOME", 1);
    const char *dirs = "XDG_DESKTOP_DIR=\"$HOME/Escritorio\"\nXDG_DOWNLOAD_DIR=\"$HOME/Descargas\"\n"
                       "XDG_DOCUMENTS_DIR=\"$HOME/Documentos\"\nXDG_PICTURES_DIR=\"$HOME/Imágenes\"\n";
    put(".config/user-dirs.dirs", dirs, strlen(dirs));
    char *mk[] = {"Escritorio", "Descargas", "Documentos", "Imágenes"};
    for (int i = 0; i < 4; i++) {
        char *p = at(mk[i]);
        g_mkdir_with_parents(p, 0700);
        free(p);
    }
    /* Un D-Bus propio desde el principio: nada toca el de tu sesión. GTestDBus
       quita XDG_RUNTIME_DIR, que el sonido necesita: se regresa. */
    const char *rt = getenv("XDG_RUNTIME_DIR");
    char *saved = rt ? xstrdup(rt) : NULL;
    g_bus = g_test_dbus_new(G_TEST_DBUS_NONE);
    g_test_dbus_up(g_bus);
    if (saved) setenv("XDG_RUNTIME_DIR", saved, 1);
    free(saved);
    desktop("org.gnome.Calculator.desktop", "Calculadora", NULL);
    desktop("com.discordapp.Discord.desktop", "Discord", NULL);
    desktop("navegador.desktop", "Navegador de prueba", "MimeType=x-scheme-handler/http;x-scheme-handler/https;\n");
    desktop("archivos.desktop", "Archivos de prueba", "MimeType=inode/directory;\n");
    desktop("oculta.desktop", "Oculta", "NoDisplay=true\n");
    /* Qué app abre qué tipo: en tu PC lo arma el sistema al instalar apps
       (update-desktop-database). */
    const char *cache = "[MIME Cache]\nx-scheme-handler/http=navegador.desktop;\n"
                        "x-scheme-handler/https=navegador.desktop;\ninode/directory=archivos.desktop;\n";
    put("share/applications/mimeinfo.cache", cache, strlen(cache));
    paths_init();
}

/* ------------------------------------------- D-Bus y apps de mentira --- */

typedef struct {
    const char *name;   /* el nombre en el bus */
    const char *status; /* "Playing", "Paused" (reproductores) o NULL (un Shell sin extensión) */
    int calls;
    char last[32];
    GMainLoop *loop;
    GThread *thread;
    volatile gint ready;
} Fake;

static void on_call(GDBusConnection *c, const char *sender, const char *path, const char *iface, const char *method,
                    GVariant *params, GDBusMethodInvocation *inv, gpointer u)
{
    Fake *f = u;
    f->calls++;
    snprintf(f->last, sizeof f->last, "%s", method);
    g_dbus_method_invocation_return_value(inv, NULL);
}

static GVariant *on_get(GDBusConnection *c, const char *sender, const char *path, const char *iface,
                        const char *prop, GError **err, gpointer u)
{
    Fake *f = u;
    return g_variant_new_string(f->status ? f->status : "Stopped");
}

static const char PLAYER_XML[] = "<node><interface name='org.mpris.MediaPlayer2.Player'>"
                                 "<method name='PlayPause'/><method name='Next'/><method name='Previous'/>"
                                 "<property name='PlaybackStatus' type='s' access='read'/>"
                                 "</interface></node>";

static gpointer fake_thread(gpointer u)
{
    Fake *f = u;
    GMainContext *ctx = g_main_context_new();
    g_main_context_push_thread_default(ctx);
    GDBusConnection *c = g_dbus_connection_new_for_address_sync(
        g_getenv("DBUS_SESSION_BUS_ADDRESS"),
        G_DBUS_CONNECTION_FLAGS_AUTHENTICATION_CLIENT | G_DBUS_CONNECTION_FLAGS_MESSAGE_BUS_CONNECTION, NULL, NULL,
        NULL);
    if (c && f->status) {
        GDBusNodeInfo *node = g_dbus_node_info_new_for_xml(PLAYER_XML, NULL);
        GDBusInterfaceVTable vt = {on_call, on_get, NULL, {0}};
        g_dbus_connection_register_object(c, "/org/mpris/MediaPlayer2", node->interfaces[0], &vt, f, NULL, NULL);
        g_dbus_node_info_unref(node);
    }
    GVariant *r = c ? g_dbus_connection_call_sync(c, "org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                  "org.freedesktop.DBus", "RequestName",
                                                  g_variant_new("(su)", f->name, 4u), NULL, G_DBUS_CALL_FLAGS_NONE,
                                                  3000, NULL, NULL)
                    : NULL;
    if (r) g_variant_unref(r);
    f->loop = g_main_loop_new(ctx, FALSE);
    g_atomic_int_set(&f->ready, 1);
    g_main_loop_run(f->loop);
    if (c) {
        g_dbus_connection_close_sync(c, NULL, NULL);
        g_object_unref(c);
    }
    g_main_loop_unref(f->loop);
    g_main_context_pop_thread_default(ctx);
    g_main_context_unref(ctx);
    return NULL;
}

static void fake_start(Fake *f)
{
    f->thread = g_thread_new("falso", fake_thread, f);
    while (!g_atomic_int_get(&f->ready)) Sleep(10);
    Sleep(100);
}

static void fake_stop(Fake *f)
{
    g_main_loop_quit(f->loop);
    g_thread_join(f->thread);
    Sleep(100);
}

/* -------------------------------------------------------------- pruebas --- */

static void test_apps(void)
{
    printf("-- abrir apps (sin GNOME: directo) --\n");
    check(starts(tool("open_app", "{\"name\":\"calculadora\"}"), "Abrí Calculadora.") &&
              wait_exists("Calculadora.abierta", 3000),
          "«calculadora»: la de GNOME aunque no se llame así en tu idioma");
    check(starts(tool("open_app", "{\"name\":\"discord\"}"), "Abrí Discord.") && wait_exists("Discord.abierta", 3000),
          "una app instalada por su nombre");
    check(has(tool("open_app", "{\"name\":\"discrod\"}"), "Parecidas instaladas: Discord"),
          "mal escrita: no abre nada y sugiere la parecida");
    check(starts(tool("open_app", "{\"name\":\"oculta\"}"), "No encontré ninguna app llamada 'oculta'"),
          "las que no se muestran en el menú (NoDisplay) no cuentan");
    check(starts(tool("open_app", "{\"name\":\"youtube.com\"}"), "Abrí https://youtube.com en el navegador.") &&
              wait_exists("Navegador de prueba.abierta", 3000),
          "una página: en tu navegador predeterminado");
    check(starts(tool("open_app", "{\"name\":\"youtube.com\",\"navegador\":\"opera\"}"),
                 "No encontré el navegador «opera» instalado"),
          "en un navegador que no está: lo dice");
    check(starts(tool("open_app", "{\"name\":\"descargas\"}"), "Abrí descargas.") &&
              wait_exists("Archivos de prueba.abierta", 3000),
          "una carpeta tuya: con tu app de archivos");
    check(starts(tool("open_app", "{\"name\":\"ms-settings:display\"}"), "Por seguridad solo abro apps"),
          "esquemas raros no");
    check(starts(tool("open_app", "{\"name\":\"//servidor/compartida\"}"), "Por seguridad no abro rutas de red."),
          "rutas de red no");
    put("Descargas/instalar.sh", "#!/bin/sh\necho hola\n", 20);
    check(starts(tool("open_app", "{\"name\":\"~/Descargas/instalar.sh\"}"),
                 "Por seguridad no abro programas ni scripts sueltos"),
          "un script suelto no");
}

static void test_peligrosos(void)
{
    printf("-- qué archivos no abre --\n");
    struct {
        const char *rel, *data;
        int mode;
        bool bad;
        const char *what;
    } C[] = {
        {"x/juego.AppImage", "algo", 0644, true, ".AppImage"},
        {"x/lanzador.desktop", "[Desktop Entry]", 0644, true, ".desktop"},
        {"x/virus.sh.", "echo", 0644, true, "«virus.sh.» (con punto al final)"},
        {"x/setup.exe", "MZ", 0644, true, ".exe (con Wine se ejecuta)"},
        {"x/programa", "\x7f" "ELF\x02\x01\x01", 0755, true, "un programa sin extensión (ELF)"},
        {"x/script", "#!/bin/bash\nrm -rf ~\n", 0755, true, "un script sin extensión con permiso de ejecutar"},
        {"x/notas", "#!/bin/bash no es\n", 0644, false, "el mismo texto sin permiso de ejecutar: se abre"},
        {"x/tarea.pdf", "%PDF-1.4\n", 0755, false, "un PDF (aunque tenga permiso de ejecutar, como en una USB)"},
        {"x/foto.jpg", "\xff\xd8\xff\xe0", 0644, false, "una foto"},
    };
    for (size_t i = 0; i < sizeof C / sizeof *C; i++) {
        put(C[i].rel, C[i].data, strlen(C[i].data));
        char *p = at(C[i].rel);
        chmod(p, (mode_t)C[i].mode);
        wchar_t *w = utf8_to_wide(p);
        check(open_target_is_dangerous(w) == C[i].bad, C[i].what);
        free(w);
        free(p);
    }
}

static void test_carpetas(void)
{
    printf("-- tus carpetas y leer --\n");
    wchar_t *d = known_folder_alias("descargas");
    char *du = wide_to_utf8(d), *want = at("Descargas");
    check(!strcmp(du, want), "«descargas» es la de XDG (~/.config/user-dirs.dirs)");
    free(du);
    free(want);
    free(d);
    put("Descargas/b.txt", "b", 1);
    put("Descargas/A.txt", "a", 1);
    put("Descargas/.oculto", "x", 1);
    char *sub = at("Descargas/carpeta");
    g_mkdir_with_parents(sub, 0700);
    free(sub);
    char *r = tool("list_files", "{\"carpeta\":\"descargas\"}");
    bool order = strstr(r, "[archivo] A.txt") && strstr(r, "[archivo] b.txt") &&
                 strstr(r, "[archivo] A.txt") < strstr(r, "[archivo] b.txt") && strstr(r, "[carpeta] carpeta");
    check(order && !strstr(r, ".oculto"), "lista en orden, dice qué es carpeta y no muestra los ocultos");
    free(r);
    const char *utf8 = "Canción: ñandú ☕\n";
    put("Documentos/utf8.txt", utf8, strlen(utf8));
    check(starts(tool("read_file", "{\"ruta\":\"~/Documentos/utf8.txt\"}"), "Canción: ñandú ☕"), "lee UTF-8");
    const unsigned char u16[] = {0xFF, 0xFE, 'H', 0, 0xF3, 0, 'l', 0, 'a', 0, 0x3D, 0xD8, 0x00, 0xDE};
    put("Documentos/u16.txt", u16, sizeof u16);
    check(starts(tool("read_file", "{\"ruta\":\"~/Documentos/u16.txt\"}"), "Hóla😀"),
          "lee UTF-16 (el «Unicode» del Bloc de notas), con emoji");
    put("Documentos/viejo.txt", "caf\xe9", 4);
    check(starts(tool("read_file", "{\"ruta\":\"~/Documentos/viejo.txt\"}"), "café"), "y Windows-1252");
    put("Documentos/bin.dat", "ab\0cd", 5);
    check(has(tool("read_file", "{\"ruta\":\"~/Documentos/bin.dat\"}"), "parece binario"), "binario: no lo lee");
    char *fifo = at("Documentos/tubo");
    mkfifo(fifo, 0600);
    free(fifo);
    uint64_t t0 = now_ms();
    char *rf = tool("read_file", "{\"ruta\":\"~/Documentos/tubo\"}");
    check(now_ms() - t0 < 2000 && strstr(rf, "no es un archivo de texto legible"),
          "una tubería (FIFO): no se queda esperando para siempre");
    free(rf);
    StrBuf big;
    sb_init(&big);
    for (int i = 0; i < 7000; i++) sb_append(&big, "ñ");
    put("Documentos/largo.txt", big.data, big.len);
    sb_free(&big);
    check(has(tool("read_file", "{\"ruta\":\"~/Documentos/largo.txt\"}"), "[...se cortó aquí, el archivo sigue...]"),
          "uno largo: lo corta y lo dice");
}

static void test_prohibidas(void)
{
    printf("-- lo que nunca toca --\n");
    put(".config/sokari/config.env", "GROQ_API_KEY=gsk_secreto\n", 25);
    check(starts(tool("read_file", "{\"ruta\":\"~/.config/sokari/config.env\"}"), "Por seguridad no uso"),
          "tu API key (~/.config/sokari)");
    char *link = at("Descargas/atajo.txt"), *target = at(".config/sokari/config.env");
    if (symlink(target, link)) printf("      (no pude crear el enlace)\n");
    check(starts(tool("read_file", "{\"ruta\":\"~/Descargas/atajo.txt\"}"), "Por seguridad no uso"),
          "ni por un enlace que apunta ahí");
    free(link);
    free(target);
    put(".local/share/sokari/memoria.json", "{}", 2);
    check(starts(tool("list_files", "{\"carpeta\":\"~/.local/share/sokari\"}"), "Por seguridad no uso"),
          "ni tu memoria (~/.local/share/sokari)");
    check(starts(tool("read_file", "{\"ruta\":\"/proc/self/environ\"}"), "Por seguridad no uso"), "ni /proc");
    check(starts(tool("list_files", "{\"carpeta\":\"//servidor/compartida\"}"), "Por seguridad no uso"),
          "ni rutas de red");
    check(starts(tool("buscar_archivo", "{\"nombre\":\"config\",\"carpeta\":\"~/.config/sokari\"}"),
                 "Por seguridad no uso"),
          "ni busca adentro");
}

static void test_buscar_mover_borrar(void)
{
    printf("-- buscar, mover y mandar a la papelera --\n");
    put("Descargas/Tarea final.pdf", "%PDF", 4);
    put("Documentos/escuela/tarea 2.docx", "x", 1);
    put(".cache/tarea oculta.txt", "x", 1);
    char *r = tool("buscar_archivo", "{\"nombre\":\"TAREA\"}");
    check(strstr(r, "Tarea final.pdf") && strstr(r, "tarea 2.docx") && !strstr(r, "oculta"),
          "busca sin importar mayúsculas en Descargas, Escritorio y Documentos (no en ocultas)");
    free(r);
    check(starts(tool("buscar_archivo", "{\"nombre\":\"banana\"}"),
                 "No encontré ningún archivo con 'banana' en Descargas, Escritorio o Documentos."),
          "si no hay, lo dice");
    check(starts(tool("mover_archivo", "{\"origen\":\"~/Descargas/Tarea final.pdf\",\"destino_carpeta\":\"documentos\"}"),
                 "Listo, moví Tarea final.pdf a documentos.") &&
              exists("Documentos/Tarea final.pdf") && !exists("Descargas/Tarea final.pdf"),
          "mueve a una de tus carpetas");
    put("Descargas/Tarea final.pdf", "otra", 4);
    check(starts(tool("mover_archivo", "{\"origen\":\"~/Descargas/Tarea final.pdf\",\"destino_carpeta\":\"documentos\"}"),
                 "Ya hay un archivo llamado 'Tarea final.pdf'"),
          "no pisa uno que ya está");
    check(starts(tool("borrar_archivo", "{\"ruta\":\"~/Descargas/Tarea final.pdf\"}"),
                 "Listo, mandé 'Tarea final.pdf' a la papelera") &&
              !exists("Descargas/Tarea final.pdf") && exists(".local/share/Trash/files/Tarea final.pdf"),
          "manda a la papelera (se puede recuperar), no borra para siempre");
    check(starts(tool("borrar_archivo", "{\"ruta\":\"descargas\"}"), "Por seguridad no mando a la papelera"),
          "no Descargas completa");
    check(starts(tool("borrar_archivo", "{\"ruta\":\"~\"}"), "Por seguridad no mando a la papelera"),
          "ni tu carpeta personal");
    check(starts(tool("borrar_archivo", "{\"ruta\":\"/usr/bin\"}"), "Por seguridad no mando a la papelera"),
          "ni el sistema");
    check(starts(tool("borrar_archivo", "{\"ruta\":\"~/.config/sokari/config.env\"}"), "Por seguridad no uso"),
          "ni la configuración de Sokari");
    check(starts(tool("borrar_archivo", "{\"ruta\":\"~/Descargas/no-existe.txt\"}"), "No encontré"),
          "algo que no existe");
}

static void test_sistema(void)
{
    printf("-- el estado de la PC --\n");
    char *r = tool("info_sistema", "{}");
    check(strstr(r, "CPU al") && strstr(r, "RAM al") && strstr(r, "disco al"), r);
    free(r);
    check(lx_keysym(VK_CONTROL) == XK_Control_L && lx_keysym('A') == 'a' && lx_keysym(VK_F1 + 4) == 0xffc2 &&
              lx_keysym(VK_OEM_PLUS) == '+' && lx_keysym(VK_LWIN) == XK_Super_L && lx_keysym(VK_RETURN) == XK_Return,
          "las teclas de Windows como las entiende GNOME (Ctrl, letras, F5, +, Super, Enter)");
}

static void test_volumen(void)
{
    printf("-- el volumen --\n");
    int before = 0;
    bool muted = false;
    if (!system_volume_get(&before, &muted)) {
        printf("      (no hay servidor de sonido: se omite)\n");
        return;
    }
    check(starts(tool("control_media", "{\"action\":\"set_volume\",\"nivel\":30}"), "Listo, volumen al 30%."),
          "pone el volumen");
    int now = -1;
    check(system_volume_get(&now, NULL) && now == 30, "y de verdad quedó en 30");
    check(starts(tool("control_media", "{\"action\":\"volume_up\"}"), "Listo, volumen al 35%."), "sube de 5 en 5");
    check(starts(tool("control_media", "{\"action\":\"volume_down\"}"), "Listo, volumen al 30%."), "y baja");
    check(starts(tool("control_media", "{\"action\":\"mute\"}"), "Listo, lo silencié."), "silencia");
    check(system_volume_get(NULL, &muted) && muted, "y de verdad quedó en silencio");
    check(starts(tool("control_media", "{\"action\":\"set_volume\",\"nivel\":40}"), "Listo, volumen al 40%.") &&
              system_volume_get(NULL, &muted) && !muted,
          "subirlo le quita el silencio (como en Windows)");
    check(starts(tool("control_media", "{\"action\":\"set_volume\",\"nivel\":150}"), "Dime un nivel"), "150 no");
    system_volume_set(before);
}

static void test_musica_y_gnome(void)
{
    printf("-- la música (MPRIS) y sin GNOME --\n");
    check(starts(tool("control_media", "{\"action\":\"play_pause\"}"), "No encontré ningún reproductor abierto"),
          "sin reproductores: lo dice");
    Fake paused = {.name = "org.mpris.MediaPlayer2.pausado", .status = "Paused"};
    Fake playing = {.name = "org.mpris.MediaPlayer2.sonando", .status = "Playing"};
    fake_start(&paused);
    fake_start(&playing);
    check(starts(tool("control_media", "{\"action\":\"next_track\"}"), "Listo.") && playing.calls == 1 &&
              !strcmp(playing.last, "Next") && paused.calls == 0,
          "«siguiente»: al que está sonando, no al que está en pausa");
    check(starts(tool("control_media", "{\"action\":\"play_pause\"}"), "Listo.") && !strcmp(playing.last, "PlayPause"),
          "pausa");
    fake_stop(&playing);
    check(starts(tool("control_media", "{\"action\":\"previous_track\"}"), "Listo.") && paused.calls == 1 &&
              !strcmp(paused.last, "Previous"),
          "si nada suena, al que está en pausa");
    fake_stop(&paused);

    check(starts(tool("type_text", "{\"texto\":\"hola\"}"), "No encontré GNOME"),
          "sin GNOME: lo dice en vez de fingir que escribió");
    Fake shell = {.name = "org.gnome.Shell"};
    fake_start(&shell);
    check(starts(tool("list_windows", "{}"), "Para eso necesito la extensión de Sokari en GNOME"),
          "con GNOME pero sin la extensión: dice cómo prenderla");
    check(starts(tool("presionar_teclas", "{\"teclas\":\"control t\"}"), "Para eso necesito la extensión"),
          "y no oprime nada");
    fake_stop(&shell);
}

int wmain(void)
{
    SetConsoleOutputCP(CP_UTF8);
    setup();
    test_apps();
    test_peligrosos();
    test_carpetas();
    test_prohibidas();
    test_buscar_mover_borrar();
    test_sistema();
    test_volumen();
    test_musica_y_gnome();
    g_test_dbus_down(g_bus);
    g_object_unref(g_bus);
    char *cmd = str_printf("rm -rf '%s'", g_home);
    if (str_starts_with(g_home, "/tmp/sokari-acciones-")) (void)!system(cmd);
    free(cmd);
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
