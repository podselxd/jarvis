#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sphere.h"
#include "util.h"

static void write_bmp(const wchar_t *dir, const wchar_t *name, const uint32_t *px, int w, int h)
{
    wchar_t path[1024];
    swprintf(path, 1024, L"%ls\\%ls.bmp", dir, name);
    size_t row = (size_t)w * 4, size = 54 + row * h;
    unsigned char *b = calloc(1, size);
    b[0] = 'B', b[1] = 'M';
    memcpy(b + 2, &(uint32_t){(uint32_t)size}, 4);
    memcpy(b + 10, &(uint32_t){54}, 4);
    memcpy(b + 14, &(uint32_t){40}, 4);
    memcpy(b + 18, &(int32_t){w}, 4);
    memcpy(b + 22, &(int32_t){-h}, 4);
    memcpy(b + 26, &(uint16_t){1}, 2);
    memcpy(b + 28, &(uint16_t){32}, 2);
    memcpy(b + 54, px, row * h);
    write_file_atomic(path, b, size);
    free(b);
}

static double bench(SphereRenderer *r, uint32_t *px, int size, SphereStyle style, float voice)
{
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    for (int i = 0; i < 30; i++) {
        double t = 2.0 + i * 0.016;
        sphere_render(r, t, t * 0.15, t, &SPHERE_IDLE, voice, voice, style, px, size, false);
    }
    QueryPerformanceCounter(&b);
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / 30;
}

/* Con la voz y el pulso al máximo, ningún estilo toca el borde de su lienzo
   (el de las líneas es más grande: sphere_room). Antes las líneas se cortaban
   en la esfera flotante en casi todos los cuadros. */
static bool never_clipped(SphereStyle style)
{
    int base = 432; /* esfera flotante en un monitor de 1080 */
    int canvas = (int)(base * sphere_room(style));
    SphereRenderer *r = sphere_create_fit(canvas, (float)base / canvas);
    int size = sphere_size(r);
    uint32_t *px = malloc(sizeof(uint32_t) * size * size);
    int touched = 0;
    for (int i = 0; i < 120; i++) {
        double t = i / 12.0;
        sphere_render(r, t, t * 0.32, t * 3.5, &SPHERE_SPEAK, 1.0f, 1.0f, style, px, size, true);
        bool edge = false;
        for (int k = 0; k < size && !edge; k++) {
            edge = (px[k] >> 24) >= 24 || (px[(size_t)(size - 1) * size + k] >> 24) >= 24 ||
                   (px[(size_t)k * size] >> 24) >= 24 || (px[(size_t)k * size + size - 1] >> 24) >= 24;
        }
        touched += edge;
    }
    printf("%s: lienzo %d para una esfera de %d, cuadros que tocan el borde: %d/120\n",
           style == SPHERE_STYLE_LINES ? "lineas" : "puntos", size, base, touched);
    free(px);
    sphere_destroy(r);
    return touched == 0;
}

int wmain(int argc, wchar_t **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    bool ok = never_clipped(SPHERE_STYLE_DOTS);
    ok = never_clipped(SPHERE_STYLE_LINES) && ok;
    if (!ok) {
        printf("FALLA: la esfera se sale de su lienzo al hablar\n");
        return 1;
    }
    int sizes[] = {600, 1080};
    for (int k = 0; k < 2; k++) {
        int size = sizes[k];
        SphereRenderer *r = sphere_create(size);
        size = sphere_size(r);
        uint32_t *px = malloc(sizeof(uint32_t) * size * size);
        printf("size %d: puntos %.2f ms, puntos hablando %.2f ms, lineas %.2f ms\n", size,
               bench(r, px, size, SPHERE_STYLE_DOTS, 0), bench(r, px, size, SPHERE_STYLE_DOTS, 0.9f),
               bench(r, px, size, SPHERE_STYLE_LINES, 0));
        if (argc > 1 && k == 0) {
            sphere_render(r, 3.0, 3.0 * 0.15, 3.0, &SPHERE_IDLE, 0, 0, SPHERE_STYLE_DOTS, px, size, false);
            write_bmp(argv[1], L"c_dots_idle", px, size, size);
            sphere_render(r, 3.0, 3.0 * 0.15, 5.0, &SPHERE_SPEAK, 0.9f, 0.9f, SPHERE_STYLE_DOTS, px, size, false);
            write_bmp(argv[1], L"c_dots_speak", px, size, size);
            sphere_render(r, 3.0, 3.0 * 0.15, 3.0, &SPHERE_IDLE, 0, 0, SPHERE_STYLE_LINES, px, size, false);
            write_bmp(argv[1], L"c_lines_idle", px, size, size);
        }
        free(px);
        sphere_destroy(r);
    }
    return 0;
}
