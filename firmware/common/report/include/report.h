#pragma once

/*
 * When a periodic reading is worth publishing (docs/protocolo.md §1, "Envio por mudança").
 *
 * Measuring and publishing are separate: the rules get every reading, and MQTT and CAN
 * only get the ones that say something new. Each one stored in the central's database is
 * a point, so a value that did not change is not sent again, except as a sign of life
 * every few minutes.
 *
 * Plain C with no ESP-IDF, used by both boards; test_host/ runs it on the PC. Times are
 * milliseconds of a monotonic clock.
 */

#include <stdbool.h>
#include <stdint.h>

/* What was last published for one reading. Zero-initialised = nothing sent yet. */
typedef struct {
    bool     sent;
    float    value;     /* the value last sent: the deadband counts from here, not from the last reading */
    uint64_t sent_ms;
} report_t;

/*
 * True when value should be published now: nothing was sent yet, it moved by at least
 * delta from the value last sent, or max_interval_ms passed since then. With delta <= 0,
 * only the time counts.
 */
bool report_due(const report_t *r, float value, float delta, uint64_t max_interval_ms, uint64_t now_ms);

/*
 * The max_interval_ms to give report_due for a reading taken every period_ms: the sign of
 * life is due half a period early. With 300 s and a reading every 60 s, the 5th reading
 * goes out even when it is a few ms early relative to the last one sent (task scheduling);
 * with exactly 300 s it would wait for the 6th about half the time, and "every 5 min at
 * most" would become 6.
 */
#define REPORT_MAX_MS_FOR_PERIOD(max_ms, period_ms)   ((uint64_t) (max_ms) - (uint64_t) (period_ms) / 2u)

/* Call after publishing. */
void report_mark(report_t *r, float value, uint64_t now_ms);

/* Forget what was sent: the next report_due is true (a device that comes back online). */
void report_reset(report_t *r);

/*
 * RMS of equal-length windows that are already RMS values (the 30 s activity of each
 * wearable heartbeat): the root of the mean of their squares.
 */
typedef struct {
    uint64_t sum_sq;
    uint32_t count;
} report_rms_t;

void report_rms_add(report_rms_t *acc, uint16_t value);

/* The RMS of what was added since the last take, rounded; 0 when nothing was. Starts over. */
uint16_t report_rms_take(report_rms_t *acc);
