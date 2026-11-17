/*
 * Host tests of button_gesture (double tap and hold): build and run with run_tests.sh.
 *
 * play() drives the detector the way comm_task does: every change of the line is
 * reported as an edge at that millisecond, and bg_tick runs every millisecond with the
 * level of the line.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "button_gesture.h"

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond) do {                                                        \
        s_checks++;                                                             \
        if (!(cond)) {                                                          \
            s_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)

#define COUNT(a)    (sizeof(a) / sizeof((a)[0]))
#define P           true    /* pressed */
#define R           false   /* released */

static const bg_config_t CFG = {
    .settle_ms = BG_SETTLE_MS,
    .tap_max_ms = BG_TAP_MAX_MS,
    .tap_gap_ms = 1000,
    .hold_ms = 3000,
};

typedef struct {
    uint32_t t;         /* ms after the start of the script */
    bool     pressed;   /* the line goes to this level (the same level again = a glitch edge) */
} step_t;

typedef struct {
    int      double_taps;
    int      holds;
    uint32_t first_double_tap;   /* ms after the start */
    uint32_t first_hold;
} outcome_t;

/* Plays the script from `start` for `duration` ms; the line starts at `level`. */
static outcome_t play_from(button_gesture_t *g, uint32_t start, bool level, const step_t *steps, size_t n,
                           uint32_t duration){
    outcome_t o = { 0 };
    size_t next = 0;
    for (uint32_t i = 0; i <= duration; i++) {
        uint32_t t = start + i;   /* wraps like the firmware's counter */
        while (next < n && steps[next].t == i) {
            level = steps[next].pressed;
            bg_on_edge(g, t);
            next++;
        }
        bg_gesture_t gesture = bg_tick(g, t, level);
        if (gesture == BG_DOUBLE_TAP && o.double_taps++ == 0) {
            o.first_double_tap = i;
        } else if (gesture == BG_HOLD && o.holds++ == 0) {
            o.first_hold = i;
        }
    }
    return o;
}

static outcome_t play(const step_t *steps, size_t n, uint32_t duration){
    button_gesture_t g;
    bg_init(&g, &CFG, false);
    return play_from(&g, 0, false, steps, n, duration);
}

static void test_single_tap(void){
    const step_t s[] = { { 100, P }, { 250, R } };
    outcome_t o = play(s, COUNT(s), 5000);
    CHECK(o.double_taps == 0 && o.holds == 0);
}

static void test_double_tap(void){
    const step_t s[] = { { 100, P }, { 250, R }, { 600, P }, { 750, R } };
    outcome_t o = play(s, COUNT(s), 5000);
    CHECK(o.double_taps == 1 && o.holds == 0);
    CHECK(o.first_double_tap == 750 + BG_SETTLE_MS);   /* once the second release has settled */
}

static void test_second_tap_just_in_time(void){
    /* Released at 250, pressed again 990 ms later. */
    const step_t s[] = { { 100, P }, { 250, R }, { 1240, P }, { 1390, R } };
    CHECK(play(s, COUNT(s), 5000).double_taps == 1);
}

static void test_second_tap_too_late(void){
    /* 1050 ms between the taps: two single taps. */
    const step_t s[] = { { 100, P }, { 250, R }, { 1300, P }, { 1450, R } };
    outcome_t o = play(s, COUNT(s), 5000);
    CHECK(o.double_taps == 0 && o.holds == 0);
}

static void test_tap_then_medium_press(void){
    /* A 1.5 s press is neither a tap nor a hold, and it clears the waiting tap. */
    const step_t s[] = { { 100, P }, { 250, R }, { 600, P }, { 2100, R }, { 2400, P }, { 2500, R } };
    outcome_t o = play(s, COUNT(s), 6000);
    CHECK(o.double_taps == 0 && o.holds == 0);
}

static void test_hold(void){
    const step_t s[] = { { 100, P }, { 10000, R } };
    outcome_t o = play(s, COUNT(s), 12000);
    CHECK(o.holds == 1);                    /* once per press, however long */
    CHECK(o.first_hold == 100 + 3000);      /* while still held */
    CHECK(o.double_taps == 0);
}

static void test_release_just_before_the_hold(void){
    /* Released at 2995 ms of press: the release is still settling at 3000 ms, so no hold. */
    const step_t s[] = { { 100, P }, { 3095, R } };
    outcome_t o = play(s, COUNT(s), 6000);
    CHECK(o.holds == 0 && o.double_taps == 0);
}

static void test_hold_then_tap(void){
    /* The release that ends a hold is not a tap, so a tap right after it is single. */
    const step_t s[] = { { 100, P }, { 4000, R }, { 4300, P }, { 4400, R } };
    outcome_t o = play(s, COUNT(s), 7000);
    CHECK(o.holds == 1 && o.double_taps == 0);
}

static void test_hold_then_double_tap(void){
    const step_t s[] = { { 100, P }, { 4000, R }, { 4300, P }, { 4400, R }, { 4700, P }, { 4800, R } };
    outcome_t o = play(s, COUNT(s), 7000);
    CHECK(o.holds == 1 && o.double_taps == 1);
}

static void test_three_and_four_taps(void){
    const step_t three[] = { { 100, P }, { 200, R }, { 400, P }, { 500, R }, { 700, P }, { 800, R } };
    CHECK(play(three, COUNT(three), 4000).double_taps == 1);   /* the third tap starts a new count */

    const step_t four[] = { { 100, P }, { 200, R }, { 400, P }, { 500, R }, { 700, P }, { 800, R },
                            { 1000, P }, { 1100, R } };
    CHECK(play(four, COUNT(four), 4000).double_taps == 2);
}

static void test_bounce(void){
    /* Contacts bouncing for a few ms on every press and release: still one double tap. */
    const step_t s[] = {
        { 100, P }, { 102, R }, { 104, P }, { 107, R }, { 109, P },
        { 250, R }, { 253, P }, { 255, R },
        { 600, P }, { 601, R }, { 603, P },
        { 750, R }, { 752, P }, { 756, R },
    };
    outcome_t o = play(s, COUNT(s), 4000);
    CHECK(o.double_taps == 1 && o.holds == 0);
}

static void test_glitch_between_taps(void){
    /* A spike that comes back to "released" within the settling is no press at all. */
    const step_t s[] = { { 100, P }, { 250, R }, { 400, P }, { 401, R }, { 600, P }, { 750, R } };
    outcome_t o = play(s, COUNT(s), 4000);
    CHECK(o.double_taps == 1);
}

static void test_late_tick(void){
    /* comm_task busy: the second press is only looked at after the gap has run out. */
    button_gesture_t g;
    bg_init(&g, &CFG, false);
    bg_on_edge(&g, 100);
    CHECK(bg_tick(&g, 130, P) == BG_NONE);
    bg_on_edge(&g, 250);
    CHECK(bg_tick(&g, 280, R) == BG_NONE);
    bg_on_edge(&g, 1240);                      /* 990 ms after the release: in time */
    CHECK(bg_tick(&g, 1300, P) == BG_NONE);    /* the gap has run out by now, the edge has not */
    bg_on_edge(&g, 1390);
    CHECK(bg_tick(&g, 1500, R) == BG_DOUBLE_TAP);
}

static void test_late_tick_second_tap_too_late(void){
    /* No tick between the taps: the gap is judged from the edge times, not by an expiry tick. */
    button_gesture_t g;
    bg_init(&g, &CFG, false);
    bg_on_edge(&g, 100);
    CHECK(bg_tick(&g, 130, P) == BG_NONE);
    bg_on_edge(&g, 250);
    CHECK(bg_tick(&g, 280, R) == BG_NONE);
    bg_on_edge(&g, 1300);                      /* 1050 ms after the release: too late */
    CHECK(bg_tick(&g, 1330, P) == BG_NONE);
    bg_on_edge(&g, 1450);
    CHECK(bg_tick(&g, 1480, R) == BG_NONE);
}

static void test_release_of_a_hold_is_no_tap(void){
    /*
     * With the defaults a hold is always longer than a tap. Even if it were not, the
     * release that ends a hold must never count as a tap.
     */
    const bg_config_t short_hold = { .settle_ms = BG_SETTLE_MS, .tap_max_ms = 1000, .tap_gap_ms = 1000,
                                     .hold_ms = 500 };
    button_gesture_t g;
    bg_init(&g, &short_hold, false);
    const step_t s[] = { { 100, P }, { 700, R }, { 900, P }, { 1000, R } };
    outcome_t o = play_from(&g, 0, false, s, COUNT(s), 3000);
    CHECK(o.holds == 1 && o.double_taps == 0);
}

static void test_edge_reported_late(void){
    /* Only a later edge time reaches the detector (a full queue): the press counts from it. */
    button_gesture_t g;
    bg_init(&g, &CFG, false);
    bg_on_edge(&g, 140);
    CHECK(bg_tick(&g, 170, P) == BG_NONE);
    CHECK(bg_tick(&g, 3139, P) == BG_NONE);
    CHECK(bg_tick(&g, 3140, P) == BG_HOLD);
}

static void test_counter_wraps(void){
    const uint32_t start = UINT32_MAX - 500;   /* 0xFFFFFFFF comes in the middle of the gestures */
    button_gesture_t g;
    bg_init(&g, &CFG, false);
    const step_t taps[] = { { 100, P }, { 250, R }, { 600, P }, { 750, R } };
    outcome_t o = play_from(&g, start, false, taps, COUNT(taps), 3000);
    CHECK(o.double_taps == 1 && o.holds == 0);

    bg_init(&g, &CFG, false);
    const step_t hold[] = { { 100, P }, { 5000, R } };
    o = play_from(&g, start, false, hold, COUNT(hold), 6000);
    CHECK(o.holds == 1 && o.first_hold == 3100);
}

static void test_down_at_boot(void){
    /* Held (or leaned on) at boot: no hold until it has been released once. */
    button_gesture_t g;
    bg_init(&g, &CFG, true);
    const step_t s[] = { { 5000, R }, { 5500, P }, { 9000, R } };
    outcome_t o = play_from(&g, 0, true, s, COUNT(s), 10000);
    CHECK(o.holds == 1 && o.first_hold == 8500);
}

static void test_ms_until_due(void){
    button_gesture_t g;
    bg_init(&g, &CFG, false);
    CHECK(bg_ms_until_due(&g, 50) == UINT32_MAX);    /* nothing going on */

    bg_on_edge(&g, 100);
    CHECK(bg_ms_until_due(&g, 110) == 20);           /* settling */
    CHECK(bg_ms_until_due(&g, 200) == 0);            /* overdue */
    CHECK(bg_tick(&g, 130, P) == BG_NONE);
    CHECK(bg_ms_until_due(&g, 130) == 2970);         /* until the hold */
    CHECK(bg_tick(&g, 3100, P) == BG_HOLD);
    CHECK(bg_ms_until_due(&g, 3100) == UINT32_MAX);  /* held on, already reported */

    bg_on_edge(&g, 4000);
    CHECK(bg_tick(&g, 4030, R) == BG_NONE);
    bg_on_edge(&g, 4200);
    CHECK(bg_tick(&g, 4230, P) == BG_NONE);
    bg_on_edge(&g, 4300);
    CHECK(bg_tick(&g, 4330, R) == BG_NONE);
    CHECK(bg_ms_until_due(&g, 4330) == UINT32_MAX);  /* a waiting tap needs no timely tick */
}

int main(void){
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "single tap: nothing",                    test_single_tap },
        { "double tap",                             test_double_tap },
        { "second tap just in time",                test_second_tap_just_in_time },
        { "second tap too late",                    test_second_tap_too_late },
        { "tap, then a medium press",               test_tap_then_medium_press },
        { "hold: once, while held",                 test_hold },
        { "released just before the hold",          test_release_just_before_the_hold },
        { "hold, then a tap",                       test_hold_then_tap },
        { "hold, then a double tap",                test_hold_then_double_tap },
        { "three and four taps",                    test_three_and_four_taps },
        { "bounce",                                 test_bounce },
        { "glitch between the taps",                test_glitch_between_taps },
        { "late tick",                              test_late_tick },
        { "late tick, second tap too late",         test_late_tick_second_tap_too_late },
        { "the release of a hold is no tap",        test_release_of_a_hold_is_no_tap },
        { "edge reported late",                     test_edge_reported_late },
        { "32-bit counter wraps",                   test_counter_wraps },
        { "down at boot",                           test_down_at_boot },
        { "time until the next tick",               test_ms_until_due },
    };

    for (size_t i = 0; i < COUNT(tests); i++) {
        int before = s_failures;
        tests[i].fn();
        printf("%s %s\n", s_failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
