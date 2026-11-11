#pragma once

/*
 * The 23 time-domain features of a window, exactly as modelo/queda/atributos.py
 * (tempo()) defines them and in the model's order (slide "ESP32 - Segmentação e
 * atributos"). Magnitudes and angles only: they do not depend on how the board is
 * strapped on.
 *
 * Plain C over esp_dsp.h (through signal_proc's moments), tested on the host against
 * the values Python computes for the same windows (test_host/).
 */

#include <stddef.h>

#include "fall_model.h"

typedef enum {
    FF_A_MEAN,                  /* |a|: the signal_proc moments */
    FF_A_RMS,
    FF_A_STD,
    FF_A_MIN,
    FF_A_MAX,
    FF_A_CREST,
    FF_A_KURTOSIS,
    FF_A_SKEWNESS,
    FF_A_ZCR,                   /* sign changes of |a| - mean, per sample */
    FF_A_PEAK_TO_PEAK,
    FF_SMA,                     /* mean of |ax| + |ay| + |az| */
    FF_FREE_FALL_MIN,           /* lowest |a| before the peak */
    FF_FREE_FALL_TO_IMPACT_S,   /* from that lowest |a| to the peak */
    FF_POST_STD,                /* |a| over the last 0.3 s: the body after the impact */
    FF_POST_MEAN,
    FF_TILT_DEG,                /* gravity in the first 0.5 s vs the last 0.3 s */
    FF_CORR_XY,                 /* |Pearson| between accelerometer axes */
    FF_CORR_XZ,
    FF_CORR_YZ,
    FF_W_MEAN,                  /* |omega|, deg/s */
    FF_W_RMS,
    FF_W_MAX,
    FF_W_ROTATION_DEG,          /* integral of |omega| over the window */
    FF_COUNT
} fall_feature_t;

_Static_assert(FF_COUNT == FALL_MODEL_N_FEATURES, "features and model must agree");

/* Python's name of each feature, to check against fall_model_feature_names. */
extern const char *const fall_feature_names[FF_COUNT];

/* Working memory of fall_features_compute(): |a| and |omega| of the window. */
typedef struct {
    float a[FALL_MODEL_WINDOW_LEN] __attribute__((aligned(16)));
    float w[FALL_MODEL_WINDOW_LEN] __attribute__((aligned(16)));
} fall_features_scratch_t;

/*
 * axes: ax, ay, az (g) and gx, gy, gz (deg/s), FALL_MODEL_WINDOW_LEN filtered samples
 * each, with the trigger peak at index FALL_MODEL_PRE.
 */
void fall_features_compute(const float (*axes)[FALL_MODEL_WINDOW_LEN], fall_features_scratch_t *scratch,
                           float out[FF_COUNT]);
