#ifndef SOKARI_NN_H
#define SOKARI_NN_H

/* Dos kernels, compilados dos veces (AVX2+FMA y genérico SSE2) y elegidos al
   arrancar según el CPU: así el .exe corre en cualquier PC x64 pero usa AVX2
   donde existe. Activaciones en layout HWC, pesos de conv en [kh][kw][Cin][Cout]. */

typedef void (*NnConvFn)(const float *in, int H, int W, int Cin, const float *wt, const float *bias, int kh, int kw,
                         int padw, int Cout, float *out, int leaky);
typedef void (*NnMatTFn)(const float *x, int n_in, const float *wt, const float *bias, int n_out, float *y);

void nn_conv_avx2(const float *in, int H, int W, int Cin, const float *wt, const float *bias, int kh, int kw, int padw,
                  int Cout, float *out, int leaky);
void nn_mat_t_avx2(const float *x, int n_in, const float *wt, const float *bias, int n_out, float *y);
void nn_conv_generic(const float *in, int H, int W, int Cin, const float *wt, const float *bias, int kh, int kw,
                     int padw, int Cout, float *out, int leaky);
void nn_mat_t_generic(const float *x, int n_in, const float *wt, const float *bias, int n_out, float *y);

#endif
