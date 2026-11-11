#include <math.h>
#include <stdbool.h>

#include "fall_features.h"
#include "signal_proc.h"

/* Same constants as modelo/queda/atributos.py: int(0.3 * FS) and int(0.5 * FS). */
#define N           FALL_MODEL_WINDOW_LEN
#define PRE         FALL_MODEL_PRE
#define TAIL        (3 * FALL_MODEL_FS_HZ / 10)   /* the body after the impact: last 0.3 s */
#define HEAD        (FALL_MODEL_FS_HZ / 2)        /* gravity before the fall: first 0.5 s */
#define TINY        1e-12f                        /* atributos.py: below this a moment counts as 0 */
#define RAD_TO_DEG  57.29577951308232f

_Static_assert(TAIL <= N - PRE && HEAD <= PRE, "feature spans must fit the window");

const char *const fall_feature_names[FF_COUNT] = {
    [FF_A_MEAN]                = "a_mean",
    [FF_A_RMS]                 = "a_rms",
    [FF_A_STD]                 = "a_std",
    [FF_A_MIN]                 = "a_min",
    [FF_A_MAX]                 = "a_max",
    [FF_A_CREST]               = "a_crest",
    [FF_A_KURTOSIS]            = "a_curtose",
    [FF_A_SKEWNESS]            = "a_assimetria",
    [FF_A_ZCR]                 = "a_zcr",
    [FF_A_PEAK_TO_PEAK]        = "a_pico_a_pico",
    [FF_SMA]                   = "sma",
    [FF_FREE_FALL_MIN]         = "queda_livre_min",
    [FF_FREE_FALL_TO_IMPACT_S] = "queda_livre_ate_impacto_s",
    [FF_POST_STD]              = "pos_std",
    [FF_POST_MEAN]             = "pos_media",
    [FF_TILT_DEG]              = "inclinacao_graus",
    [FF_CORR_XY]               = "corr_xy",
    [FF_CORR_XZ]               = "corr_xz",
    [FF_CORR_YZ]               = "corr_yz",
    [FF_W_MEAN]                = "w_mean",
    [FF_W_RMS]                 = "w_rms",
    [FF_W_MAX]                 = "w_max",
    [FF_W_ROTATION_DEG]        = "w_rotacao_graus",
};

static float mean_of(const float *x, size_t from, size_t to){
    float sum = 0.0f;
    for (size_t i = from; i < to; i++) {
        sum += x[i];
    }
    return sum / (float) (to - from);
}

/* |Pearson correlation| between two axes; 0 when either one is flat. */
static float abs_correlation(const float *x, const float *y){
    float mx = mean_of(x, 0, N);
    float my = mean_of(y, 0, N);
    float sxy = 0.0f, sxx = 0.0f, syy = 0.0f;
    for (size_t i = 0; i < N; i++) {
        float dx = x[i] - mx;
        float dy = y[i] - my;
        sxy += dx * dy;
        sxx += dx * dx;
        syy += dy * dy;
    }
    float den = sqrtf(sxx * syy);
    return den > TINY ? fabsf(sxy / den) : 0.0f;
}

/* Angle between the mean acceleration (gravity) of two stretches of the window. */
static float tilt_deg(const float (*axes)[N]){
    float u[3], v[3];
    for (int k = 0; k < 3; k++) {
        u[k] = mean_of(axes[k], 0, HEAD);
        v[k] = mean_of(axes[k], N - TAIL, N);
    }
    float dot = u[0] * v[0] + u[1] * v[1] + u[2] * v[2];
    float norms = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]) * sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    float c = dot / (norms > 1e-9f ? norms : 1e-9f);
    c = c > 1.0f ? 1.0f : (c < -1.0f ? -1.0f : c);
    return acosf(c) * RAD_TO_DEG;
}

void fall_features_compute(const float (*axes)[FALL_MODEL_WINDOW_LEN], fall_features_scratch_t *scratch,
                           float out[FF_COUNT]){
    const float *ax = axes[0], *ay = axes[1], *az = axes[2];
    const float *gx = axes[3], *gy = axes[4], *gz = axes[5];
    float *a = scratch->a;
    float *w = scratch->w;

    float sma = 0.0f;
    float rotation = 0.0f;
    for (size_t i = 0; i < N; i++) {
        a[i] = sqrtf(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
        w[i] = sqrtf(gx[i] * gx[i] + gy[i] * gy[i] + gz[i] * gz[i]);
        sma += fabsf(ax[i]) + fabsf(ay[i]) + fabsf(az[i]);
        rotation += w[i];
    }

    /* Moments of |a|: the same signal_proc code the window log has always used. */
    sp_features_t m;
    sp_features_compute(a, N, &m);
    out[FF_A_MEAN] = m.mean;
    out[FF_A_RMS] = m.rms;
    out[FF_A_STD] = m.std;
    out[FF_A_MIN] = m.min;
    out[FF_A_MAX] = m.max;
    out[FF_A_CREST] = m.crest;
    out[FF_A_PEAK_TO_PEAK] = m.max - m.min;

    /* Central moments and zero crossings of |a| - mean, in one pass. */
    float m2 = 0.0f, m3 = 0.0f, m4 = 0.0f;
    uint32_t crossings = 0;
    bool was_negative = false;
    for (size_t i = 0; i < N; i++) {
        float c = a[i] - m.mean;
        float c2 = c * c;
        m2 += c2;
        m3 += c2 * c;
        m4 += c2 * c2;
        bool negative = signbit(c) != 0;
        if (i > 0 && negative != was_negative) {
            crossings++;
        }
        was_negative = negative;
    }
    m2 /= (float) N;
    m3 /= (float) N;
    m4 /= (float) N;
    out[FF_A_KURTOSIS] = m2 > TINY ? m4 / (m2 * m2) : 0.0f;
    out[FF_A_SKEWNESS] = m2 > TINY ? m3 / (m2 * sqrtf(m2)) : 0.0f;
    out[FF_A_ZCR] = (float) crossings / (float) (N - 1);
    out[FF_SMA] = sma / (float) N;

    /* Free fall: the lowest |a| before the peak (the first one, as numpy's argmin). */
    size_t lowest = 0;
    for (size_t i = 1; i < PRE; i++) {
        if (a[i] < a[lowest]) {
            lowest = i;
        }
    }
    out[FF_FREE_FALL_MIN] = a[lowest];
    out[FF_FREE_FALL_TO_IMPACT_S] = (float) (PRE - lowest) / (float) FALL_MODEL_FS_HZ;

    /* After the impact: still (small deviation) and lying (mean |a| near 1 g)? */
    float post_mean = mean_of(a, N - TAIL, N);
    float post_var = 0.0f;
    for (size_t i = N - TAIL; i < N; i++) {
        float c = a[i] - post_mean;
        post_var += c * c;
    }
    out[FF_POST_STD] = sqrtf(post_var / (float) TAIL);
    out[FF_POST_MEAN] = post_mean;

    out[FF_TILT_DEG] = tilt_deg(axes);
    out[FF_CORR_XY] = abs_correlation(ax, ay);
    out[FF_CORR_XZ] = abs_correlation(ax, az);
    out[FF_CORR_YZ] = abs_correlation(ay, az);

    sp_features_t mw;
    sp_features_compute(w, N, &mw);
    out[FF_W_MEAN] = mw.mean;
    out[FF_W_RMS] = mw.rms;
    out[FF_W_MAX] = mw.max;
    out[FF_W_ROTATION_DEG] = rotation / (float) FALL_MODEL_FS_HZ;
}
