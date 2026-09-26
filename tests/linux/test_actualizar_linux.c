/* El actualizador de Linux, sin internet: qué paquete del release baja (el de
   este sistema), cuándo una versión es más nueva y que solo instala el
   paquete publicado (su tipo, su tamaño y su huella SHA-256). */
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "linux/linux.h"
#include "third_party/cJSON.h"
#include "update.h"
#include "util.h"

static int g_fail, g_total;
static char g_dir[512];

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    fflush(stdout);
    if (!ok) g_fail++;
}

/* Un paquete de mentira de n bytes que empieza con magic; su huella en sha. */
static char *fake_package(const char *name, const char *magic, size_t n, char sha[65])
{
    char *path = str_printf("%s/%s", g_dir, name);
    unsigned char *data = malloc(n);
    for (size_t i = 0; i < n; i++) data[i] = (unsigned char)(i * 31 + 7);
    memcpy(data, magic, strlen(magic));
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(data, 1, n, f);
        fclose(f);
    }
    unsigned char sum[32];
    sha256_bytes(data, n, sum);
    char *hex = hex_encode(sum, sizeof sum);
    snprintf(sha, 65, "%s", hex);
    free(hex);
    free(data);
    return path;
}

/* ¿Lo instalaría? Si no, que el porqué diga lo que se esperaba. */
static bool verify(const char *path, double size, const char *sha, const char *kind, const char *reason)
{
    char *error = NULL;
    bool ok = linux_package_verify(path, size, sha, kind, &error);
    if (!ok && reason && (!error || !strstr(error, reason))) {
        printf("      dijo: %s\n", error ? error : "(nada)");
        ok = true; /* lo rechazó, pero por otra razón: cuenta como falla */
    }
    free(error);
    return ok;
}

static void test_pick(void)
{
    printf("-- qué paquete baja --\n");
    const char *kind = linux_package_kind();
    check(kind != NULL, "sabe si este sistema se instala con .deb o con .rpm");
    if (!kind) return;
    cJSON *all = cJSON_Parse("[{\"name\":\"Sokari.exe\"},{\"name\":\"Sokari.apk\"},{\"name\":\"Sokari.deb\"},"
                             "{\"name\":\"Sokari.rpm\"}]");
    const cJSON *a = update_pick_asset(all);
    const cJSON *name = cJSON_GetObjectItem(a, "name");
    char *want = str_printf("Sokari.%s", kind), *what = str_printf("de un release con los cuatro, baja %s", want);
    check(cJSON_IsString(name) && !strcmp(name->valuestring, want), what);
    free(what);
    free(want);
    cJSON_Delete(all);
    cJSON *windows = cJSON_Parse("[{\"name\":\"Sokari.exe\"},{\"name\":\"Sokari.apk\"}]");
    check(update_pick_asset(windows) == NULL && update_pick_asset(NULL) == NULL,
          "si el release no trae el paquete de este sistema: nada (nunca el .exe)");
    cJSON_Delete(windows);
}

static void test_versions(void)
{
    printf("-- cuándo hay versión nueva --\n");
    check(linux_version_newer("v2.5.1", "2.5.0"), "v2.5.1 es más nueva que 2.5.0");
    check(linux_version_newer("v2.10.0", "2.9.3"), "v2.10.0 es más nueva que 2.9.3 (números, no letras)");
    check(linux_version_newer("v3.0.0", "2.5.0"), "v3.0.0 es más nueva que 2.5.0");
    check(!linux_version_newer("v2.5.0", "2.5.0"), "la misma versión no es nueva");
    check(!linux_version_newer("v2.4.9", "2.5.0"), "una más vieja no es nueva");
}

static void test_verify(void)
{
    printf("-- solo instala el paquete publicado --\n");
    char deb_sha[65], rpm_sha[65], small_sha[65], text_sha[65];
    const size_t n = 200000;
    char *deb = fake_package("Sokari.deb", "!<arch>\n", n, deb_sha);
    char *rpm = fake_package("Sokari.rpm", "\xed\xab\xee\xdb", n, rpm_sha);
    char *small = fake_package("chico.deb", "!<arch>\n", 5000, small_sha);
    char *text = fake_package("pagina.deb", "<!DOCTYPE html>", n, text_sha);
    char *missing = str_printf("%s/no-existe.deb", g_dir);

    check(verify(deb, n, deb_sha, "deb", NULL), "un .deb con su tamaño y su huella: sí");
    check(verify(rpm, n, rpm_sha, "rpm", NULL), "un .rpm con su tamaño y su huella: sí");
    check(verify(deb, 0, deb_sha, "deb", NULL), "si GitHub no dio el tamaño, basta la huella");
    char upper[65];
    for (int i = 0; i < 65; i++) upper[i] = (char)(deb_sha[i] >= 'a' && deb_sha[i] <= 'f' ? deb_sha[i] - 32 : deb_sha[i]);
    check(verify(deb, n, upper, "deb", NULL), "la huella en mayúsculas también cuenta");

    char other[65];
    snprintf(other, sizeof other, "%s", deb_sha);
    other[0] = other[0] == '0' ? '1' : '0';
    check(!verify(deb, n, other, "deb", "huella"), "con otra huella: no");
    check(!verify(deb, n, NULL, "deb", "huella"), "si GitHub no publicó la huella: no");
    check(!verify(deb, n + 1, deb_sha, "deb", "válido"), "si no mide lo que dice GitHub: no");
    check(!verify(deb, n, deb_sha, "rpm", "válido"), "un .deb cuando este sistema usa .rpm: no");
    check(!verify(rpm, n, rpm_sha, "deb", "válido"), "un .rpm cuando este sistema usa .deb: no");
    check(!verify(text, n, text_sha, "deb", "válido"), "una página de error con su huella: no");
    check(!verify(small, 5000, small_sha, "deb", "válido"), "algo demasiado chico para ser Sokari: no");
    check(!verify(missing, n, deb_sha, "deb", "válido"), "si no se bajó nada: no");

    const char *files[] = {deb, rpm, small, text};
    for (int i = 0; i < 4; i++) unlink(files[i]);
    free(deb);
    free(rpm);
    free(small);
    free(text);
    free(missing);
}

int wmain(void)
{
    const char *tmp = getenv("TMPDIR");
    snprintf(g_dir, sizeof g_dir, "%s/sokari-actualizar-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(g_dir)) {
        perror("mkdtemp");
        return 2;
    }
    test_pick();
    test_versions();
    test_verify();
    rmdir(g_dir);
    printf("%d/%d pruebas ok\n", g_total - g_fail, g_total);
    return g_fail ? 1 : 0;
}
