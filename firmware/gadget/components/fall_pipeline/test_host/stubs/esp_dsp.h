#pragma once

/*
 * Host stand-in for ESP-DSP: only what fall_pipeline and signal_proc call, with the
 * semantics of the library's portable (_ansi) versions, which the optimized ones must
 * match.
 */

static inline int dsps_dotprod_f32(const float *src1, const float *src2, float *dest, int len){
    float acc = 0;
    for (int i = 0; i < len; i++) {
        acc += src1[i] * src2[i];
    }
    *dest = acc;   /* assigns, does not accumulate into *dest */
    return 0;
}

/* Direct form II, coef = {b0, b1, b2, a1, a2}, w = the two delays (dsps_biquad_f32_ansi.c). */
static inline int dsps_biquad_f32(const float *input, float *output, int len, float *coef, float *w){
    for (int i = 0; i < len; i++) {
        float d0 = input[i] - coef[3] * w[0] - coef[4] * w[1];
        output[i] = coef[0] * d0 + coef[1] * w[0] + coef[2] * w[1];
        w[1] = w[0];
        w[0] = d0;
    }
    return 0;
}
