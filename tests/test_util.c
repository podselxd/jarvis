/* Las utilidades que cada sistema implementa a su modo (util_win.c en
   Windows, src/linux/util_linux.c en Linux): texto ancho, minúsculas con
   acentos, SHA-256, fechas, archivos y rutas. La misma prueba corre en los
   dos, así que lo que Sokari guarda y lee se comporta igual. Todo en una
   carpeta temporal. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#include "util.h"

static int g_fail, g_total;

static void check(bool ok, const char *what)
{
    g_total++;
    printf("%s %s\n", ok ? "ok   " : "FALLA", what);
    if (!ok) g_fail++;
}

static void test_texto(void)
{
    printf("-- texto ancho y minúsculas --\n");
    const char *s = "ñandú ☕ 𝄞 «hola»";
    wchar_t *w = utf8_to_wide(s);
    char *back = wide_to_utf8(w);
    check(!strcmp(back, s), "UTF-8 -> ancho -> UTF-8 queda igual (con ñ, acentos, emoji y fuera del plano básico)");
    free(back);
    free(w);
    w = utf8_to_wide("a\xff" "b");
    check(w[0] == L'a' && w[1] == 0xFFFD && w[2] == L'b' && !w[3], "un byte que no es UTF-8 queda como U+FFFD");
    free(w);
    char *low = str_lower("ÁÉÍÓÚÑÜ ABC Canción");
    check(!strcmp(low, "áéíóúñü abc canción"), "minúsculas con acentos y ñ");
    free(low);
    check(str_contains_ci("La CANCIÓN de hoy", "canción") && str_eq_ci("ÑANDÚ", "ñandú"),
          "comparar sin mayúsculas también con acentos");
    char *c = cp1252_to_utf8("\x93Hola\x94 \x80 caf\xe9", 13);
    check(!strcmp(c, "\xe2\x80\x9cHola\xe2\x80\x9d \xe2\x82\xac caf\xc3\xa9"), "Windows-1252 a UTF-8 (comillas, € y é)");
    free(c);
    check(utf8_truncate_len("añb", 2) == 1, "cortar UTF-8 sin partir una letra");
}

static void test_sha(void)
{
    printf("-- SHA-256 --\n");
    char *h = sha256_hex("");
    check(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "vacío");
    free(h);
    h = sha256_hex("abc");
    check(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "«abc»");
    free(h);
    h = sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
    check(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
          "56 letras (el relleno ocupa un bloque extra)");
    free(h);
    StrBuf sb;
    sb_init(&sb);
    for (int i = 0; i < 100; i++) sb_append(&sb, "ñ");
    h = sha256_hex(sb.data);
    check(!strcmp(h, "3a08bcbf26569b380862d1c76ef0f5f0a65d5080159e8e0239899359d0520fbe"), "200 bytes (varios bloques)");
    free(h);
    sb_free(&sb);
    unsigned char r1[16] = {0}, r2[16] = {0};
    random_bytes(r1, sizeof r1);
    random_bytes(r2, sizeof r2);
    check(memcmp(r1, r2, sizeof r1) != 0, "dos azares seguidos no se parecen");
}

static void test_fechas(void)
{
    printf("-- fechas --\n");
    double t = 0;
    check(parse_iso_local("2026-09-25T10:30:00Z", &t) && t == 1790332200.0, "con Z es UTC");
    check(parse_iso_local("2026-09-25T10:30:00+02:00", &t) && t == 1790325000.0, "con zona +02:00");
    check(parse_iso_local("2026-01-15 08:05:00", &t), "hora local sin zona");
    char *back = format_epoch_local(t, "%Y-%m-%d %H:%M:%S");
    check(!strcmp(back, "2026-01-15 08:05:00"), "y de vuelta a texto da la misma hora local");
    free(back);
    check(!parse_iso_local("2026-13-01", &t) && !parse_iso_local("mañana", &t), "fechas inválidas no pasan");
    double now = now_epoch();
    check(now > 1.7e9 && now < 4e9, "now_epoch es de este siglo");
    uint64_t a = now_ms();
    Sleep(50);
    check(now_ms() - a >= 30, "now_ms avanza");
}

static void test_archivos(void)
{
    printf("-- archivos y rutas --\n");
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    unsigned char rnd[4];
    random_bytes(rnd, sizeof rnd);
    char *name = str_printf("sokari_util_%02x%02x%02x%02x", rnd[0], rnd[1], rnd[2], rnd[3]);
    wchar_t *wname = utf8_to_wide(name);
    wchar_t *root = path_join(tmp, wname);
    wchar_t *nested = path_join(root, L"a");
    wchar_t *deep = path_join(nested, L"b");
    check(ensure_dir(deep) && dir_exists(deep) && !file_exists(deep), "crear carpetas anidadas");
    wchar_t *f = path_join(deep, L"datos ñ.txt");
    const char *text = "hola\nmundo";
    check(write_file_atomic(f, text, strlen(text)) && file_exists(f) && file_size(f) == 10, "escribir y tamaño");
    size_t n = 0;
    char *got = read_file_all(f, &n);
    check(got && n == 10 && !strcmp(got, text), "leer lo escrito");
    free(got);
    check(append_file(f, "!", 1) && file_size(f) == 11, "agregar al final");
    wchar_t *g = path_join(deep, L"copia.txt");
    check(copy_file(f, g, false) && file_size(g) == 11, "copiar");
    check(!copy_file(f, g, false), "copiar sin reemplazar no pisa lo que ya existe");
    wchar_t *h = path_join(nested, L"movido.txt");
    check(move_file(g, h) && !file_exists(g) && file_exists(h), "mover");
    check(file_size(g) == -1, "tamaño de algo que no existe: -1");
#ifndef _WIN32
    char *p = wide_to_utf8(f);
    struct stat st;
    check(!stat(p, &st) && (st.st_mode & 0777) == 0600, "en Linux lo que escribe Sokari solo lo lee tu usuario (0600)");
    free(p);
    char *d = wide_to_utf8(deep);
    check(!stat(d, &st) && (st.st_mode & 0777) == 0700, "y sus carpetas, 0700");
    free(d);
#endif
    check(!wcscmp(path_basename(f), L"datos ñ.txt"), "nombre del archivo");
    wchar_t *dn = path_dirname(f);
    check(!wcscmp(dn, deep), "carpeta del archivo");
    free(dn);
#ifdef _WIN32
    wchar_t *e = expand_env(L"%USERPROFILE%\\x");
    check(wcslen(e) > 3 && !wcsstr(e, L"%"), "expandir %USERPROFILE%");
#else
    wchar_t *e = expand_env(L"~/x");
    wchar_t *home = utf8_to_wide(getenv("HOME"));
    wchar_t *want = path_join(home, L"x");
    check(!wcscmp(e, want), "expandir ~");
    free(e);
    e = expand_env(L"%USERPROFILE%/x");
    check(!wcscmp(e, want), "%USERPROFILE% en Linux es tu carpeta personal");
    free(e);
    e = expand_env(L"$HOME/x");
    check(!wcscmp(e, want), "expandir $HOME");
    free(home);
    free(want);
#endif
    free(e);
    DeleteFileW(f);
    DeleteFileW(h);
    RemoveDirectoryW(deep);
    RemoveDirectoryW(nested);
    RemoveDirectoryW(root);
    check(!dir_exists(root), "limpieza");
    free(f);
    free(g);
    free(h);
    free(deep);
    free(nested);
    free(root);
    free(wname);
    free(name);
}

int wmain(void)
{
    SetConsoleOutputCP(CP_UTF8);
    test_texto();
    test_sha();
    test_fechas();
    test_archivos();
    printf("%d/%d pruebas %s\n", g_total - g_fail, g_total, g_fail ? "— HAY FALLAS" : "ok");
    return g_fail ? 1 : 0;
}
