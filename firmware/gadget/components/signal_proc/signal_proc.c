#include <math.h>
#include <string.h>

#include "signal_proc.h"
#include "esp_dsp.h"

/* ---------------------------------------------------------------------------
 * Circular buffer
 * ------------------------------------------------------------------------- */

void sp_ring_init(sp_ring_t *ring, float *storage, size_t cap){
    ring->buf = storage;
    ring->cap = cap;
    ring->head = 0;
    ring->count = 0;
}

void sp_ring_push(sp_ring_t *ring, float x){
    if (ring->cap == 0) {
        return;
    }
    ring->buf[ring->head] = x;
    ring->head = (ring->head + 1) % ring->cap;
    if (ring->count < ring->cap) {
        ring->count++;
    }
}

size_t sp_ring_copy_last(const sp_ring_t *ring, size_t n, float *out){
    if (n > ring->count) {
        n = ring->count;
    }
    if (n == 0) {
        return 0;
    }
    /* The oldest of the n newest samples, then at most two contiguous runs. */
    size_t start = (ring->head + ring->cap - n) % ring->cap;
    size_t first = ring->cap - start;
    if (first > n) {
        first = n;
    }
    memcpy(out, &ring->buf[start], first * sizeof(float));
    memcpy(out + first, ring->buf, (n - first) * sizeof(float));
    return n;
}

/* ---------------------------------------------------------------------------
 * Window features
 * ------------------------------------------------------------------------- */

void sp_features_compute(const float *x, size_t n, sp_features_t *out){
    memset(out, 0, sizeof(*out));
    if (n == 0) {
        return;
    }

    float sum = 0.0f;
    float min = x[0];
    float max = x[0];
    for (size_t i = 0; i < n; i++) {
        sum += x[i];
        if (x[i] < min) min = x[i];
        if (x[i] > max) max = x[i];
    }

    /* Sum of squares as the dot product of the window with itself (slide pattern). */
    float sum_sq = 0.0f;
    dsps_dotprod_f32(x, x, &sum_sq, (int) n);

    float mean = sum / (float) n;
    float mean_sq = sum_sq / (float) n;
    float var = mean_sq - mean * mean;

    out->n = (uint32_t) n;
    out->mean = mean;
    out->rms = sqrtf(mean_sq);
    out->std = var > 0.0f ? sqrtf(var) : 0.0f;   /* rounding can leave a tiny negative */
    out->min = min;
    out->max = max;
    out->crest = out->rms > 0.0f ? max / out->rms : 0.0f;
}

/* ---------------------------------------------------------------------------
 * Activity
 * ------------------------------------------------------------------------- */

void sp_activity_add(sp_activity_t *activity, float magnitude_g){
    double deviation = (double) magnitude_g - 1.0;
    activity->sum += deviation * deviation;
    activity->n++;
}

void sp_activity_merge(sp_activity_t *into, const sp_activity_t *from){
    into->sum += from->sum;
    into->n += from->n;
}

uint16_t sp_activity_take_mg(sp_activity_t *activity){
    if (activity->n == 0) {
        return 0;
    }
    double mg = sqrt(activity->sum / activity->n) * 1000.0;
    activity->sum = 0;
    activity->n = 0;
    return mg > 65535.0 ? 65535 : (uint16_t) (mg + 0.5);
}
