#ifndef SOKARI_SPHERE_H
#define SOKARI_SPHERE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SPHERE_STYLE_DOTS = 0,  /* halo de puntos (default) */
    SPHERE_STYLE_LINES = 1, /* meridianos, el diseño de Main.dc.html */
} SphereStyle;

typedef struct {
    float low[3];  /* colorLow  (0-255) */
    float high[3]; /* colorHigh (0-255) */
    float rotation_speed;
    float ripple;
    float glow; /* radio del resplandor, en píxeles del lienzo original de 960 */
} SphereParams;

extern const SphereParams SPHERE_IDLE;
extern const SphereParams SPHERE_SPEAK;

typedef struct SphereRenderer SphereRenderer;

SphereRenderer *sphere_create(int size);
/* fit (0..1]: la esfera se dibuja a esa fracción de su tamaño normal y lo
   demás del lienzo queda como espacio (transparente en la esfera flotante)
   para que las ondas no se corten. sphere_create(size) usa fit = 1. */
SphereRenderer *sphere_create_fit(int size, float fit);
/* Cuánto más grande tiene que ser el lienzo, con la esfera del mismo tamaño,
   para que ese estilo nunca se salga al hablar: las líneas se deforman mucho
   más que los puntos. Se usa con sphere_create_fit(lienzo, 1 / room). */
float sphere_room(SphereStyle style);
void sphere_destroy(SphereRenderer *r);
int sphere_size(const SphereRenderer *r);

/* Dibuja un cuadro size x size en out (BGRA, stride en píxeles). voice (0..1)
   es la energía de la voz de Sokari en este instante: agranda la esfera,
   agrega una ondulación rápida y más destellos. pulse (0..1) es esa misma voz
   sílaba por sílaba (sube y baja rápido): en el halo de puntos hace que la
   esfera palpite, se agrande y se aclare hacia blanco con cada sílaba.
   Con premultiplied, el alfa sale de la intensidad (ventana flotante
   transparente); si no, fondo negro opaco. */
void sphere_render(SphereRenderer *r, double t, double angle, double voice_t, const SphereParams *p, float voice,
                   float pulse, SphereStyle style, uint32_t *out, int stride, bool premultiplied);

void sphere_lerp(SphereParams *out, const SphereParams *a, const SphereParams *b, float f);

#endif
