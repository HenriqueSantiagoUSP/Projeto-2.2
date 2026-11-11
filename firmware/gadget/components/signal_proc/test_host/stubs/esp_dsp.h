#pragma once

/*
 * Host stand-in for ESP-DSP: only what signal_proc.c calls, with the semantics of
 * the library's portable (_ansi) version, which the optimized ones must match.
 */

static inline int dsps_dotprod_f32(const float *src1, const float *src2, float *dest, int len){
    float acc = 0;
    for (int i = 0; i < len; i++) {
        acc += src1[i] * src2[i];
    }
    *dest = acc;   /* assigns, does not accumulate into *dest */
    return 0;
}
