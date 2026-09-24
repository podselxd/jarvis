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
        sphere_render(r, t, t * 0.15, t, &SPHERE_IDLE, voice, style, px, size, false);
    }
    QueryPerformanceCounter(&b);
    return (double)(b.QuadPart - a.QuadPart) * 1000.0 / f.QuadPart / 30;
}

int wmain(int argc, wchar_t **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
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
            sphere_render(r, 3.0, 3.0 * 0.15, 3.0, &SPHERE_IDLE, 0, SPHERE_STYLE_DOTS, px, size, false);
            write_bmp(argv[1], L"c_dots_idle", px, size, size);
            sphere_render(r, 3.0, 3.0 * 0.15, 5.0, &SPHERE_SPEAK, 0.9f, SPHERE_STYLE_DOTS, px, size, false);
            write_bmp(argv[1], L"c_dots_speak", px, size, size);
            sphere_render(r, 3.0, 3.0 * 0.15, 3.0, &SPHERE_IDLE, 0, SPHERE_STYLE_LINES, px, size, false);
            write_bmp(argv[1], L"c_lines_idle", px, size, size);
        }
        free(px);
        sphere_destroy(r);
    }
    return 0;
}
