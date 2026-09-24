/* Incluido por nn_avx2.c y nn_generic.c con NN_SFX definido. */
#include <string.h>

#include "nn.h"

#define NN_CAT2(a, b) a##_##b
#define NN_CAT(a, b) NN_CAT2(a, b)
#define NN_FN(name) NN_CAT(name, NN_SFX)

void NN_FN(nn_conv)(const float *restrict in, int H, int W, int Cin, const float *restrict wt,
                    const float *restrict bias, int kh, int kw, int padw, int Cout, float *restrict out, int leaky)
{
    int Ho = H - kh + 1, Wo = W + 2 * padw - kw + 1;
    for (int h = 0; h < Ho; h++) {
        for (int w = 0; w < Wo; w++) {
            float *restrict o = out + ((size_t)h * Wo + w) * Cout;
            if (bias)
                memcpy(o, bias, sizeof(float) * (size_t)Cout);
            else
                memset(o, 0, sizeof(float) * (size_t)Cout);
            for (int i = 0; i < kh; i++) {
                for (int j = 0; j < kw; j++) {
                    int ww = w + j - padw;
                    if (ww < 0 || ww >= W) continue;
                    const float *restrict x = in + ((size_t)(h + i) * W + ww) * Cin;
                    const float *restrict k = wt + (size_t)(i * kw + j) * Cin * Cout;
                    for (int c = 0; c < Cin; c++) {
                        float xv = x[c];
                        const float *restrict kk = k + (size_t)c * Cout;
                        for (int oc = 0; oc < Cout; oc++) o[oc] += xv * kk[oc];
                    }
                }
            }
            if (leaky) {
                for (int oc = 0; oc < Cout; oc++) {
                    float v = o[oc];
                    if (v < 0) {
                        v *= 0.2f;
                        if (v < -0.4f) v = -0.4f;
                    }
                    o[oc] = v;
                }
            }
        }
    }
}

void NN_FN(nn_mat_t)(const float *restrict x, int n_in, const float *restrict wt, const float *restrict bias,
                     int n_out, float *restrict y)
{
    if (bias)
        memcpy(y, bias, sizeof(float) * (size_t)n_out);
    else
        memset(y, 0, sizeof(float) * (size_t)n_out);
    for (int i = 0; i < n_in; i++) {
        float xv = x[i];
        if (xv == 0.0f) continue;
        const float *restrict row = wt + (size_t)i * n_out;
        for (int o = 0; o < n_out; o++) y[o] += xv * row[o];
    }
}
