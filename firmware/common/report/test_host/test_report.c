/*
 * Host tests of report (publish on change, at least every few minutes; RMS of windows):
 * build and run with run_tests.sh.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "report.h"

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond) do {                                                        \
        s_checks++;                                                             \
        if (!(cond)) {                                                          \
            s_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)

#define DELTA       0.2f
#define MAX_MS      (5u * 60u * 1000u)
#define MIN_MS(m)   ((uint64_t) (m) * 60u * 1000u)

/* What the hub does with each reading: publish when due. Returns how many were published. */
static int feed(report_t *r, const float *values, int n, uint64_t period_ms){
    int sent = 0;
    for (int i = 0; i < n; i++) {
        uint64_t now = (uint64_t) i * period_ms;
        if (report_due(r, values[i], DELTA, MAX_MS, now)) {
            report_mark(r, values[i], now);
            sent++;
        }
    }
    return sent;
}

static void test_first_reading_goes(void){
    report_t r = { 0 };
    CHECK(report_due(&r, 22.0f, DELTA, MAX_MS, 0));
    report_mark(&r, 22.0f, 0);
    CHECK(!report_due(&r, 22.0f, DELTA, MAX_MS, 1000));
}

static void test_small_changes_stay(void){
    report_t r = { 0 };
    report_mark(&r, 22.0f, 0);
    CHECK(!report_due(&r, 22.1f, DELTA, MAX_MS, 60000));
    CHECK(!report_due(&r, 21.9f, DELTA, MAX_MS, 60000));
}

static void test_big_changes_go_up_and_down(void){
    report_t r = { 0 };
    report_mark(&r, 22.0f, 0);
    CHECK(report_due(&r, 22.3f, DELTA, MAX_MS, 60000));
    CHECK(report_due(&r, 21.7f, DELTA, MAX_MS, 60000));
}

static void test_change_of_exactly_delta_goes(void){
    /* 0.25 and 22.25 are exact in binary: no rounding decides the edge. */
    report_t r = { 0 };
    report_mark(&r, 22.0f, 0);
    CHECK(report_due(&r, 22.25f, 0.25f, MAX_MS, 60000));
    CHECK(report_due(&r, 21.75f, 0.25f, MAX_MS, 60000));
    CHECK(!report_due(&r, 22.125f, 0.25f, MAX_MS, 60000));
}

static void test_slow_drift_is_sent(void){
    /* 0.06 degC per reading: never 0.2 between two readings, but it adds up from the value sent. */
    const float drift[] = { 22.00f, 22.06f, 22.12f, 22.18f, 22.24f };
    report_t r = { 0 };
    CHECK(feed(&r, drift, 5, 60000) == 2);
    CHECK(r.value == 22.24f);
}

static void test_sign_of_life(void){
    report_t r = { 0 };
    report_mark(&r, 22.0f, 1000);
    CHECK(!report_due(&r, 22.0f, DELTA, MAX_MS, 1000 + MAX_MS - 1));
    CHECK(report_due(&r, 22.0f, DELTA, MAX_MS, 1000 + MAX_MS));
}

static void test_time_only(void){
    /* delta <= 0: the value does not matter, as for the wearable summary. */
    report_t r = { 0 };
    report_mark(&r, 0.0f, 0);
    CHECK(!report_due(&r, 999.0f, 0.0f, MAX_MS, MAX_MS - 1));
    CHECK(report_due(&r, 999.0f, 0.0f, MAX_MS, MAX_MS));
}

static void test_reset(void){
    report_t r = { 0 };
    report_mark(&r, 22.0f, 0);
    report_reset(&r);
    CHECK(report_due(&r, 22.0f, DELTA, MAX_MS, 1));
}

static void test_one_steady_hour(void){
    /* A reading a minute, wobbling 0.05 degC: one message every 5 min, not 60. */
    float values[60];
    for (int i = 0; i < 60; i++) {
        values[i] = 22.0f + ((i % 2) ? 0.05f : -0.05f);
    }
    report_t r = { 0 };
    CHECK(feed(&r, values, 60, MIN_MS(1)) == 12);
}

static void test_one_hour_of_ramp(void){
    /* 0.06 degC a minute (3.6 degC/h): a message every 4 min, when the change reaches 0.24 degC. */
    float values[60];
    for (int i = 0; i < 60; i++) {
        values[i] = 20.0f + 0.06f * (float) i;
    }
    report_t r = { 0 };
    CHECK(feed(&r, values, 60, MIN_MS(1)) == 15);
}

static void test_rms(void){
    report_rms_t acc = { 0 };
    CHECK(report_rms_take(&acc) == 0);          /* nothing added */
    report_rms_add(&acc, 120);
    CHECK(report_rms_take(&acc) == 120);
    report_rms_add(&acc, 300);
    report_rms_add(&acc, 400);
    CHECK(report_rms_take(&acc) == 354);        /* sqrt((300^2 + 400^2) / 2) = 353.6 */
    CHECK(report_rms_take(&acc) == 0);          /* the take starts over */

    for (int i = 0; i < 10; i++) {
        report_rms_add(&acc, UINT16_MAX);       /* no overflow at the top of the range */
    }
    CHECK(report_rms_take(&acc) == UINT16_MAX);
}

int main(void){
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "first reading goes",              test_first_reading_goes },
        { "small changes stay",              test_small_changes_stay },
        { "big changes go, up and down",     test_big_changes_go_up_and_down },
        { "a change of exactly delta goes",  test_change_of_exactly_delta_goes },
        { "slow drift is sent",              test_slow_drift_is_sent },
        { "sign of life every 5 min",        test_sign_of_life },
        { "time only",                       test_time_only },
        { "reset",                           test_reset },
        { "one steady hour: 12 messages",    test_one_steady_hour },
        { "one hour of ramp",                test_one_hour_of_ramp },
        { "RMS of windows",                  test_rms },
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = s_failures;
        tests[i].fn();
        printf("%s %s\n", s_failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
