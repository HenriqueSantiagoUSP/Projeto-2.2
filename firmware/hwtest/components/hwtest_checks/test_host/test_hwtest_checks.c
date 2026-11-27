/*
 * Host tests of hwtest_checks (settled inputs, window statistics, the verdict of each part of
 * the board): build and run with run_tests.sh.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "hwtest_checks.h"

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond) do {                                                        \
        s_checks++;                                                             \
        if (!(cond)) {                                                          \
            s_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)

#define NEAR(a, b, tol)   (fabsf((float) (a) - (float) (b)) <= (tol))

/* The board's chain (lm35.h), so the numbers below read like the real ones. */
static const lm35_chain_t CHAIN = {
    .slope_mv_per_c = 51.3335f,
    .offset_mv = 144.739f,
    .min_c = 0.0f,
    .max_c = 57.0f,
};

/* ---------------------------------------------------------------------------
 * Inputs
 * ------------------------------------------------------------------------- */

static void test_first_level_needs_the_settle_time(void){
    input_track_t t;
    input_track_init(&t, 50, 0);
    CHECK(!input_track_feed(&t, 1, 0));
    CHECK(!input_track_feed(&t, 1, 49));
    CHECK(t.stable == -1);
    CHECK(input_track_feed(&t, 1, 50));
    CHECK(t.stable == 1);
    CHECK(t.seen_high && !t.seen_low);
    CHECK(t.rises == 0 && t.falls == 0);   /* the first level is not an edge */
}

static void test_bounce_is_ignored(void){
    input_track_t t;
    input_track_init(&t, 50, 0);
    input_track_feed(&t, 1, 0);
    input_track_feed(&t, 1, 60);           /* settled open */
    /* A contact bouncing for 30 ms, shorter than the settle time each time. */
    uint32_t now = 100;
    for (int i = 0; i < 6; i++, now += 5) {
        CHECK(!input_track_feed(&t, i % 2 == 0 ? 0 : 1, now));
    }
    CHECK(t.stable == 1);
    CHECK(!t.seen_low);
    /* Then it really closes. */
    input_track_feed(&t, 0, 200);
    CHECK(!input_track_feed(&t, 0, 249));
    CHECK(input_track_feed(&t, 0, 250));
    CHECK(t.stable == 0 && t.falls == 1 && t.rises == 0);
    CHECK(t.seen_low && t.seen_high);
}

static void test_edges_are_counted(void){
    input_track_t t;
    input_track_init(&t, 30, 0);
    uint32_t now = 0;
    int level = 1;
    for (int i = 0; i < 7; i++) {         /* 1, 0, 1, 0, 1, 0, 1 */
        input_track_feed(&t, level, now);
        input_track_feed(&t, level, now + 30);
        now += 100;
        level = !level;
    }
    CHECK(t.stable == 1);
    CHECK(t.falls == 3);
    CHECK(t.rises == 3);
}

static void test_restart_keeps_the_current_level(void){
    input_track_t t;
    input_track_init(&t, 30, 0);
    input_track_feed(&t, 1, 0);
    input_track_feed(&t, 1, 30);
    input_track_feed(&t, 0, 100);
    input_track_feed(&t, 0, 130);
    CHECK(t.seen_low && t.seen_high && t.falls == 1);
    input_track_restart(&t);
    CHECK(t.seen_low && !t.seen_high);
    CHECK(t.falls == 0 && t.rises == 0);
    CHECK(t.stable == 0);
}

/* ---------------------------------------------------------------------------
 * Window
 * ------------------------------------------------------------------------- */

static void test_window_statistics(void){
    float storage[4];
    window_t w;
    window_init(&w, storage, 4);
    window_summary_t s = window_summary(&w);
    CHECK(s.n == 0);

    const float values[] = { 2.0f, 4.0f, 4.0f, 4.0f };
    for (int i = 0; i < 4; i++) {
        window_push(&w, values[i]);
    }
    s = window_summary(&w);
    CHECK(s.n == 4);
    CHECK(NEAR(s.mean, 3.5f, 1e-5f));
    CHECK(NEAR(s.std, 0.8660254f, 1e-5f));   /* population: sqrt(0.75) */
    CHECK(s.min == 2.0f && s.max == 4.0f);

    /* Full: the oldest (2.0) goes out first. */
    window_push(&w, 4.0f);
    s = window_summary(&w);
    CHECK(s.n == 4);
    CHECK(NEAR(s.mean, 4.0f, 1e-5f));
    CHECK(NEAR(s.std, 0.0f, 1e-5f));
    CHECK(s.min == 4.0f);

    window_clear(&w);
    CHECK(window_summary(&w).n == 0);
}

/* ---------------------------------------------------------------------------
 * LM35
 * ------------------------------------------------------------------------- */

static window_summary_t steady_mv(float mv, float std, size_t n){
    return (window_summary_t) { .n = n, .mean = mv, .std = std, .min = mv - std, .max = mv + std };
}

static void test_lm35(void){
    window_summary_t none = { 0 };
    CHECK(check_lm35(&CHAIN, &none).state == CHECK_WAITING);

    /* 25 C through the chain: 51.33 * 25 + 144.7 = 1428 mV. */
    window_summary_t room = steady_mv(1428.1f, 1.0f, 100);
    CHECK(NEAR(lm35_celsius(&CHAIN, room.mean), 25.0f, 0.01f));
    check_verdict_t v = check_lm35(&CHAIN, &room);
    CHECK(v.state == CHECK_OK && v.why == NULL);

    /* No 5 V on the board side: the LM358 output sits near 0 V. */
    window_summary_t dead = steady_mv(20.0f, 1.0f, 100);
    CHECK(check_lm35(&CHAIN, &dead).state == CHECK_FAIL);

    /* Above the 57 C the ADC can read: a fault, not a temperature. */
    window_summary_t hot = steady_mv(51.3335f * 60.0f + 144.739f, 1.0f, 100);
    CHECK(check_lm35(&CHAIN, &hot).state == CHECK_FAIL);
    /* Just inside the range is fine. */
    window_summary_t warm = steady_mv(51.3335f * 56.0f + 144.739f, 1.0f, 100);
    CHECK(check_lm35(&CHAIN, &warm).state == CHECK_OK);

    /* Noise: 0.3 C of spread is reported, but the sensor works. */
    window_summary_t noisy = steady_mv(1428.0f, 0.3f * 51.3335f, 100);
    CHECK(lm35_noisy(&CHAIN, &noisy));
    v = check_lm35(&CHAIN, &noisy);
    CHECK(v.state == CHECK_OK && v.why != NULL);
    /* 0.1 C is not noise worth a word. */
    window_summary_t quiet = steady_mv(1428.0f, 0.1f * 51.3335f, 100);
    CHECK(!lm35_noisy(&CHAIN, &quiet));
    /* With a couple of readings the spread means nothing yet. */
    window_summary_t few = steady_mv(1428.0f, 0.5f * 51.3335f, 3);
    CHECK(!lm35_noisy(&CHAIN, &few));
}

/* ---------------------------------------------------------------------------
 * PIR, doors, button
 * ------------------------------------------------------------------------- */

static void test_both_levels(void){
    input_track_t t;
    input_track_init(&t, 30, 0);
    input_track_feed(&t, 1, 0);
    input_track_feed(&t, 1, 30);
    check_verdict_t v = check_both_levels(&t, "feche o contato");
    CHECK(v.state == CHECK_WAITING && v.why != NULL);
    input_track_feed(&t, 0, 100);
    input_track_feed(&t, 0, 130);
    CHECK(check_both_levels(&t, "feche o contato").state == CHECK_OK);
}

static void test_pir_waits_for_the_warm_up(void){
    input_track_t t;
    input_track_init(&t, 50, 0);
    /* Moving during the warm-up proves nothing. */
    input_track_feed(&t, 1, 0);
    input_track_feed(&t, 1, 50);
    input_track_feed(&t, 0, 100);
    input_track_feed(&t, 0, 150);
    CHECK(check_pir(&t, 59999, 60000).state == CHECK_WAITING);
    /* main.c restarts the tracker when the warm-up ends: the clear it shows then counts. */
    input_track_restart(&t);
    check_verdict_t v = check_pir(&t, 60000, 60000);
    CHECK(v.state == CHECK_WAITING && v.why != NULL);
    input_track_feed(&t, 1, 61000);
    input_track_feed(&t, 1, 61050);
    CHECK(check_pir(&t, 61050, 60000).state == CHECK_OK);   /* clear, then a detection */

    /* Warm-up ending while it detects: it must be seen back to clear. */
    input_track_t busy;
    input_track_init(&busy, 50, 0);
    input_track_feed(&busy, 1, 59000);
    input_track_feed(&busy, 1, 59050);
    input_track_restart(&busy);
    v = check_pir(&busy, 60000, 60000);
    CHECK(v.state == CHECK_WAITING && v.why != NULL);
    input_track_feed(&busy, 0, 65000);
    input_track_feed(&busy, 0, 65050);
    CHECK(check_pir(&busy, 65050, 60000).state == CHECK_OK);
}

/* ---------------------------------------------------------------------------
 * MPU-6050 and its INT
 * ------------------------------------------------------------------------- */

static void test_mpu(void){
    CHECK(check_mpu(false, 200.0f, 200.0f, true).state == CHECK_FAIL);
    CHECK(check_mpu(true, -1.0f, 200.0f, false).state == CHECK_WAITING);
    CHECK(check_mpu(true, 0.0f, 200.0f, false).state == CHECK_FAIL);
    CHECK(check_mpu(true, 100.0f, 200.0f, true).state == CHECK_FAIL);
    CHECK(check_mpu(true, 199.0f, 200.0f, false).state == CHECK_WAITING);
    CHECK(check_mpu(true, 199.0f, 200.0f, true).state == CHECK_OK);
    CHECK(check_mpu(true, 181.0f, 200.0f, true).state == CHECK_OK);
    CHECK(check_mpu(true, 179.0f, 200.0f, true).state == CHECK_FAIL);

    CHECK(mpu_at_rest(1.0f, 0.5f));
    CHECK(mpu_at_rest(0.95f, 2.0f));
    CHECK(!mpu_at_rest(1.0f, 30.0f));      /* turning */
    CHECK(!mpu_at_rest(1.4f, 1.0f));       /* shaken */
    CHECK(!mpu_at_rest(0.25f, 1.0f));      /* +-2 g read as +-8 g, or falling */
}

static void test_mpu_int(void){
    CHECK(check_mpu_int(false, 0.0f, 200.0f).state == CHECK_WAITING);
    CHECK(check_mpu_int(true, -1.0f, 200.0f).state == CHECK_WAITING);
    CHECK(check_mpu_int(true, 0.0f, 200.0f).state == CHECK_FAIL);
    CHECK(check_mpu_int(true, 200.0f, 200.0f).state == CHECK_OK);
    CHECK(check_mpu_int(true, 60.0f, 200.0f).state == CHECK_FAIL);
}

/* ---------------------------------------------------------------------------
 * CAN
 * ------------------------------------------------------------------------- */

static void test_can(void){
    CHECK(check_can(false, 0, 0).state == CHECK_FAIL);
    CHECK(check_can(true, 0, 0).state == CHECK_WAITING);
    CHECK(check_can(true, 2, 0).state == CHECK_WAITING);
    CHECK(check_can(true, 3, 0).state == CHECK_FAIL);
    CHECK(check_can(true, 3, 1).state == CHECK_OK);
    CHECK(check_can(true, 500, 480).state == CHECK_OK);
}

/* ---------------------------------------------------------------------------
 * Summary
 * ------------------------------------------------------------------------- */

static void test_summary(void){
    check_verdict_t v[3] = {
        { CHECK_OK, NULL }, { CHECK_WAITING, "x" }, { CHECK_OK, NULL },
    };
    size_t ok = 99;
    CHECK(check_summary(v, 3, &ok) == SUMMARY_WAITING && ok == 2);
    v[1].state = CHECK_OK;
    CHECK(check_summary(v, 3, &ok) == SUMMARY_ALL_OK && ok == 3);
    v[0].state = CHECK_FAIL;
    CHECK(check_summary(v, 3, &ok) == SUMMARY_FAILED && ok == 2);
    v[2].state = CHECK_WAITING;
    CHECK(check_summary(v, 3, NULL) == SUMMARY_FAILED);   /* a failure wins over waiting */
}

int main(void){
    const struct {
        const char *name;
        void (*fn)(void);
    } tests[] = {
        { "first level needs the settle time", test_first_level_needs_the_settle_time },
        { "bounce is ignored",                  test_bounce_is_ignored },
        { "edges are counted",                  test_edges_are_counted },
        { "restart keeps the current level",    test_restart_keeps_the_current_level },
        { "window statistics",                  test_window_statistics },
        { "LM35",                               test_lm35 },
        { "both levels",                        test_both_levels },
        { "PIR waits for the warm-up",          test_pir_waits_for_the_warm_up },
        { "MPU-6050",                           test_mpu },
        { "MPU INT",                            test_mpu_int },
        { "CAN",                                test_can },
        { "summary",                            test_summary },
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = s_failures;
        tests[i].fn();
        printf("%s %s\n", s_failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
