#pragma once

/*
 * Signal processing building blocks of the wearable, following aulas/ slides
 * "ESP32 - Amostragem" and "ESP32 - Segmentação e atributos":
 *
 *  - sp_ring_t: circular buffer (fixed array + head index: nothing is copied on
 *    insert), so the window around a trigger can be taken after the fact; the fall
 *    pipeline keeps one per axis.
 *  - sp_features_t: the moments of a window (mean, RMS, deviation, min, max, crest),
 *    with ESP-DSP for the sums; the first 6 features of the fall pipeline.
 *  - sp_activity_t: RMS of (|a| - 1 g) between two heartbeats.
 *
 * Plain C over esp_dsp.h, tested on the host (test_host/).
 */

#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Circular buffer
 * ------------------------------------------------------------------------- */

typedef struct {
    float  *buf;
    size_t  cap;
    size_t  head;     /* next write position */
    size_t  count;    /* valid samples, up to cap */
} sp_ring_t;

/* storage must hold cap floats and outlive the ring. */
void sp_ring_init(sp_ring_t *ring, float *storage, size_t cap);

/* Writes one sample over the oldest one once the ring is full. */
void sp_ring_push(sp_ring_t *ring, float x);

/* Copies the newest n samples (fewer if the ring holds fewer), oldest first, into
   out; returns how many were copied. */
size_t sp_ring_copy_last(const sp_ring_t *ring, size_t n, float *out);

/* ---------------------------------------------------------------------------
 * Window features
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t n;
    float    mean;
    float    rms;
    float    std;      /* population standard deviation */
    float    min;      /* free fall shows up here: |a| near 0 g */
    float    max;      /* the impact */
    float    crest;    /* max / rms */
} sp_features_t;

/* x should be 16-byte aligned for the ESP-DSP fast path. n == 0 gives all zeros. */
void sp_features_compute(const float *x, size_t n, sp_features_t *out);

/* ---------------------------------------------------------------------------
 * Activity
 * ------------------------------------------------------------------------- */

typedef struct {
    double   sum;     /* sum of (|a| - 1 g)^2 */
    uint32_t n;
} sp_activity_t;

void sp_activity_add(sp_activity_t *activity, float magnitude_g);

/* Adds everything accumulated in `from` to `into`. */
void sp_activity_merge(sp_activity_t *into, const sp_activity_t *from);

/* RMS of (|a| - 1 g) since the previous take, in mg; restarts the average. */
uint16_t sp_activity_take_mg(sp_activity_t *activity);
