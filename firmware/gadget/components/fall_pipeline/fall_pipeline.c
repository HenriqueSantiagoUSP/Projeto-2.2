#include <math.h>
#include <string.h>

#include "esp_dsp.h"
#include "fall_pipeline.h"

#define N   FALL_MODEL_WINDOW_LEN

/* ---------------------------------------------------------------------------
 * [2] Trigger
 * ------------------------------------------------------------------------- */

void fp_trigger_init(fp_trigger_t *trigger, float l_on_g){
    memset(trigger, 0, sizeof(*trigger));
    trigger->l_on = l_on_g;
}

bool fp_trigger_feed(fp_trigger_t *trigger, float a, uint32_t n, uint32_t *peak_index, float *peak){
    if (!trigger->started) {
        trigger->started = true;
        trigger->prev = a;   /* the first sample is no rise */
    }

    bool final = false;
    if (trigger->tracking) {
        if (a > trigger->peak) {
            trigger->peak = a;
            trigger->peak_index = n;
        } else if (n - trigger->peak_index >= FALL_MODEL_TRIGGER_GAP) {
            /* Unsigned subtraction: correct across the wrap of the sample count. */
            trigger->tracking = false;
            *peak_index = trigger->peak_index;
            *peak = trigger->peak;
            final = true;
        }
    }
    /* A new peak starts on a rise to L_on, never on the way down from the last one. */
    if (!trigger->tracking && a >= trigger->l_on && a > trigger->prev) {
        trigger->tracking = true;
        trigger->peak = a;
        trigger->peak_index = n;
    }
    trigger->prev = a;
    return final;
}

/* ---------------------------------------------------------------------------
 * Pipeline
 * ------------------------------------------------------------------------- */

void fall_pipeline_init(fall_pipeline_t *fp, float l_on_g, fp_classifier_t classifier){
    memset(fp, 0, sizeof(*fp));
    fp->classifier = classifier;
    memcpy(fp->coef, fall_model_biquad, sizeof(fp->coef));
    fp_trigger_init(&fp->trigger, l_on_g);
    for (int ch = 0; ch < FP_CHANNELS; ch++) {
        sp_ring_init(&fp->ring[ch], fp->ring_storage[ch], N);
    }
}

/*
 * Starts each delay line as if its first sample had always been there, so the filter
 * does not ring from zero (the zi * x[0] of the Python side). For the direct form II
 * of dsps_biquad_f32 that is w = x0 / (1 + a1 + a2) in both delays.
 */
static void filter_start(fall_pipeline_t *fp, const float *sample){
    float gain = 1.0f + fp->coef[3] + fp->coef[4];
    for (int ch = 0; ch < FP_CHANNELS; ch++) {
        fp->delay[ch][0] = sample[ch] / gain;
        fp->delay[ch][1] = sample[ch] / gain;
    }
}

/* The newest N samples of each axis, oldest first; right after boot the oldest repeats. */
static void copy_window(fall_pipeline_t *fp){
    for (int ch = 0; ch < FP_CHANNELS; ch++) {
        float *w = fp->window[ch];
        size_t got = sp_ring_copy_last(&fp->ring[ch], N, w);
        if (got < N) {
            memmove(w + (N - got), w, got * sizeof(float));
            for (size_t i = 0; i < N - got; i++) {
                w[i] = w[N - got];
            }
        }
    }
    fp->window_peak_index = fp->pending_index;
    fp->window_a_peak = fp->pending_a;
}

bool fall_pipeline_feed(fall_pipeline_t *fp, const float sample[FP_CHANNELS]){
    if (!fp->started) {
        filter_start(fp, sample);
        fp->started = true;
    }

    float f[FP_CHANNELS];
    for (int ch = 0; ch < FP_CHANNELS; ch++) {
        dsps_biquad_f32(&sample[ch], &f[ch], 1, fp->coef, fp->delay[ch]);
        sp_ring_push(&fp->ring[ch], f[ch]);
    }

    uint32_t n = fp->n++;
    float a = sqrtf(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
    uint32_t peak_index;
    float peak;
    if (fp_trigger_feed(&fp->trigger, a, n, &peak_index, &peak)) {
        fp->pending = true;
        fp->pending_index = peak_index;
        fp->pending_a = peak;
    }

    if (fp->pending && n - fp->pending_index == FALL_MODEL_POST - 1) {
        copy_window(fp);
        fp->pending = false;
        return true;
    }
    return false;
}

void fall_pipeline_classify(fall_pipeline_t *fp, fp_result_t *out){
    fall_features_compute((const float (*)[N]) fp->window, &fp->scratch, out->features);
    out->peak_index = fp->window_peak_index;
    out->a_peak = fp->window_a_peak;
    out->prob = fp_classifier_prob(fp->classifier, out->features);
    out->fall = out->prob >= fp_classifier_threshold(fp->classifier);
}
