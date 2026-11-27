#pragma once

/*
 * The judgement of the hardware test (firmware/hwtest): no driver here, so it runs on the
 * PC too (test_host/). main.c reads the pins and the buses and feeds these functions; they
 * say whether each part of the board was seen working, and what to do when it was not.
 *
 * The "why" texts are shown to people on the serial panel: Portuguese, plain ASCII (the
 * idf.py monitor console on Windows does not always speak UTF-8).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CHECK_WAITING,   /* nothing wrong seen, but the part was not proven yet (or needs an action) */
    CHECK_OK,
    CHECK_FAIL,
} check_state_t;

typedef struct {
    check_state_t state;
    const char   *why;    /* what is missing or wrong; NULL when there is nothing to say */
} check_verdict_t;

/* ---------------------------------------------------------------------------
 * A digital input: settled level, edges, and whether both levels were seen
 * ------------------------------------------------------------------------- */

typedef struct {
    uint32_t settle_ms;
    int      stable;             /* -1 until the first settled reading, then 0 or 1 */
    int      raw;                /* last raw level fed */
    uint32_t raw_since_ms;       /* when the raw level last changed */
    uint32_t rises, falls;       /* settled 0 -> 1 and 1 -> 0 */
    bool     seen_low, seen_high;
} input_track_t;

void input_track_init(input_track_t *t, uint32_t settle_ms, uint32_t now_ms);
/* Feeds the raw level; a level counts once it held for settle_ms. True when the settled level changed. */
bool input_track_feed(input_track_t *t, int level, uint32_t now_ms);
/* Forgets the edges and the levels seen, keeping the current settled level as seen. */
void input_track_restart(input_track_t *t);

/* ---------------------------------------------------------------------------
 * Statistics over the last N values
 * ------------------------------------------------------------------------- */

typedef struct {
    float  *buf;
    size_t  cap, len, head;
} window_t;

typedef struct {
    size_t n;
    float  mean, std, min, max;   /* std is the population standard deviation */
} window_summary_t;

void window_init(window_t *w, float *storage, size_t cap);
void window_push(window_t *w, float value);
void window_clear(window_t *w);
window_summary_t window_summary(const window_t *w);

/* ---------------------------------------------------------------------------
 * Verdicts
 * ------------------------------------------------------------------------- */

/* Below this the LM358 output is near 0 V: no 5 V on the board side (PCB USB-C) or no LM35. */
#define CHECK_LM35_NO_SIGNAL_MV     100.0f
/* Noise above this would make the hub publish a change that is not there (MQTT_TEMP_REPORT_DELTA_C). */
#define CHECK_LM35_NOISE_MAX_C      0.2f

typedef struct {
    float slope_mv_per_c;     /* the chain fit (lm35.h) */
    float offset_mv;
    float min_c, max_c;       /* valid range (lm35.h) */
} lm35_chain_t;

float lm35_celsius(const lm35_chain_t *chain, float mv);
check_verdict_t check_lm35(const lm35_chain_t *chain, const window_summary_t *mv);
/* True when the readings scatter more than CHECK_LM35_NOISE_MAX_C (needs a few readings). */
bool lm35_noisy(const lm35_chain_t *chain, const window_summary_t *mv);

/* Both levels seen: the input moves. wait_hint says what to do until then. */
check_verdict_t check_both_levels(const input_track_t *t, const char *wait_hint);
/* The PIR means nothing during its warm-up; after it, like check_both_levels. */
check_verdict_t check_pir(const input_track_t *t, uint32_t now_ms, uint32_t warmup_ms);

/* MPU-6050 sample rate the hardware test expects (MPU6050_SAMPLE_RATE_HZ), and the tolerance. */
#define CHECK_MPU_RATE_TOL          0.1f     /* +-10 % */
/* |a| at rest: gravity alone. */
#define CHECK_MPU_REST_MIN_G        0.9f
#define CHECK_MPU_REST_MAX_G        1.1f

/* True when a block of samples looks like the board lying still: |a| ~ 1 g and little rotation. */
bool mpu_at_rest(float amag_g, float gyro_dps);
check_verdict_t check_mpu(bool found, float rate_hz, float expected_hz, bool seen_rest);
/* Pulses per second on the MPU INT pin, against the sample rate. */
check_verdict_t check_mpu_int(bool mpu_found, float pulses_hz, float expected_hz);

/* Frames sent (queued) and frames read back through the transceiver (bench mode) or from others. */
check_verdict_t check_can(bool init_ok, uint32_t sent, uint32_t echoed);

/* ---------------------------------------------------------------------------
 * Summary
 * ------------------------------------------------------------------------- */

typedef enum {
    SUMMARY_WAITING,   /* nothing failed, something still to prove */
    SUMMARY_ALL_OK,
    SUMMARY_FAILED,    /* at least one failure */
} summary_t;

summary_t check_summary(const check_verdict_t *verdicts, size_t count, size_t *ok_count);
