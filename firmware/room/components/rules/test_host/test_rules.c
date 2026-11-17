/*
 * Host tests of the alert rules: build and run with run_tests.sh (gcc, no ESP-IDF).
 * Each test replays a scenario on a fake clock and checks the exact alert events.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rules.h"

#define MIN(m)   ((uint64_t) (m) * 60u * 1000u)

static int s_failures = 0;
static int s_checks = 0;

#define CHECK(cond) do {                                                        \
        s_checks++;                                                             \
        if (!(cond)) {                                                          \
            s_failures++;                                                       \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
        }                                                                       \
    } while (0)

/* ---- recorded alerts ---- */

#define MAX_EVENTS 32
static rules_alert_t s_events[MAX_EVENTS];
static int s_event_count;

static void record(const rules_alert_t *alert, void *ctx){
    (void) ctx;
    if (s_event_count < MAX_EVENTS) {
        s_events[s_event_count++] = *alert;
    }
}

static int count(alert_type_t type, alert_state_t state){
    int n = 0;
    for (int i = 0; i < s_event_count; i++) {
        if (s_events[i].type == type && s_events[i].state == state) n++;
    }
    return n;
}

static const rules_alert_t *last(void){
    return s_event_count ? &s_events[s_event_count - 1] : NULL;
}

/* ---- fake clock: starts 2026-10-20 at the given local time ---- */

static rules_clock_t s_clock;
static const int64_t EPOCH_BASE = 1792454400;   /* 2026-10-20 00:00 local, as if UTC */

static void clock_at(int hour, int minute){
    s_clock.time_valid = true;
    s_clock.minute_of_day = (uint16_t) (hour * 60 + minute);
    s_clock.epoch_s = EPOCH_BASE + s_clock.minute_of_day * 60;
}

static void advance(rules_t *r, uint64_t ms){
    /* Tick once per second of simulated time, as the hub does. */
    for (uint64_t t = 0; t < ms; t += 1000) {
        s_clock.mono_ms += 1000;
        if (s_clock.mono_ms % 60000 == 0) {
            s_clock.minute_of_day = (uint16_t) ((s_clock.minute_of_day + 1) % 1440);
        }
        s_clock.epoch_s += 1;
        rules_tick(r, &s_clock);
    }
}

/* ---- fixture: door 0 = bedroom (exit), door 1 = bathroom ---- */

static const char *const DOOR_IDS[] = { "quarto", "banheiro" };

static void setup(rules_t *r, int hour, int minute){
    memset(&s_clock, 0, sizeof(s_clock));
    s_clock.mono_ms = 1000;
    clock_at(hour, minute);
    s_event_count = 0;

    rules_params_t p;
    rules_default_params(&p);
    p.door_role[0] = DOOR_ROLE_EXIT;
    p.door_role[1] = DOOR_ROLE_BATHROOM;
    rules_init(r, &p, DOOR_IDS, 2, record, NULL, &s_clock);

    /* Boot reports: both doors closed, nobody moving. */
    rules_on_door(r, 0, false, &s_clock);
    rules_on_door(r, 1, false, &s_clock);
    rules_on_presence(r, false, &s_clock);
}

/* ---- tests ---- */

static void test_boot_state_is_not_an_event(void){
    rules_t r;
    setup(&r, 23, 0);
    /* The bathroom door was already closed at boot: that must not arm the rule. */
    advance(&r, MIN(60));
    CHECK(count(ALERT_BATHROOM, ALERT_ACTIVE) == 0);
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 0);
}

static void test_night_exit_fires_and_resolves(void){
    rules_t r;
    setup(&r, 23, 0);

    rules_on_presence(&r, true, &s_clock);          /* resident walks to the door */
    rules_on_door(&r, 0, true, &s_clock);
    advance(&r, 5000);
    rules_on_presence(&r, false, &s_clock);         /* PIR releases inside the grace */
    rules_on_door(&r, 0, false, &s_clock);

    advance(&r, MIN(14));
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 0);
    advance(&r, MIN(1));
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 1);
    CHECK(strcmp(last()->origin, "porta/quarto") == 0);
    CHECK(strcmp(last()->door_id, "quarto") == 0);
    CHECK(last()->minutes == 15);

    advance(&r, MIN(10));
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 1);   /* not repeated */

    char id[RULES_ALERT_ID_LEN];
    snprintf(id, sizeof(id), "%s", last()->id);
    rules_on_door(&r, 0, true, &s_clock);           /* comes back */
    rules_on_presence(&r, true, &s_clock);
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_RESOLVED) == 1);
    CHECK(strcmp(last()->id, id) == 0);
}

static void test_night_exit_cleared_by_presence_inside(void){
    rules_t r;
    setup(&r, 23, 0);
    rules_on_door(&r, 0, true, &s_clock);           /* opens the door, stays in the room */
    advance(&r, MIN(2));
    rules_on_presence(&r, true, &s_clock);
    advance(&r, MIN(30));
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 0);
}

static void test_night_exit_ignored_by_day(void){
    rules_t r;
    setup(&r, 15, 0);
    rules_on_door(&r, 0, true, &s_clock);
    advance(&r, MIN(30));
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 0);
}

static void test_night_exit_needs_wall_clock(void){
    rules_t r;
    setup(&r, 23, 0);
    s_clock.time_valid = false;
    rules_on_door(&r, 0, true, &s_clock);
    advance(&r, MIN(30));
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 0);
}

static void test_bathroom_fires_and_resolves(void){
    rules_t r;
    setup(&r, 10, 0);
    rules_on_presence(&r, true, &s_clock);
    rules_on_door(&r, 1, true, &s_clock);
    rules_on_door(&r, 1, false, &s_clock);          /* shuts herself in */
    advance(&r, 20000);
    rules_on_presence(&r, false, &s_clock);

    advance(&r, MIN(39));
    CHECK(count(ALERT_BATHROOM, ALERT_ACTIVE) == 0);
    advance(&r, MIN(1));
    CHECK(count(ALERT_BATHROOM, ALERT_ACTIVE) == 1);
    CHECK(strcmp(last()->origin, "porta/banheiro") == 0);

    rules_on_door(&r, 1, true, &s_clock);
    CHECK(count(ALERT_BATHROOM, ALERT_RESOLVED) == 1);
}

static void test_bathroom_cleared_when_resident_is_in_bedroom(void){
    rules_t r;
    setup(&r, 10, 0);
    rules_on_door(&r, 1, true, &s_clock);
    rules_on_door(&r, 1, false, &s_clock);          /* closed it from the bedroom side */
    advance(&r, MIN(5));
    rules_on_presence(&r, true, &s_clock);
    advance(&r, MIN(60));
    CHECK(count(ALERT_BATHROOM, ALERT_ACTIVE) == 0);
}

static void test_bathroom_cleared_when_resident_leaves(void){
    rules_t r;
    setup(&r, 10, 0);
    rules_on_door(&r, 1, true, &s_clock);
    rules_on_door(&r, 1, false, &s_clock);
    advance(&r, MIN(1));
    rules_on_door(&r, 0, true, &s_clock);           /* left through the bedroom door */
    advance(&r, MIN(60));
    CHECK(count(ALERT_BATHROOM, ALERT_ACTIVE) == 0);
}

static void test_inactivity_by_day_only(void){
    rules_t r;
    setup(&r, 9, 0);
    rules_on_presence(&r, true, &s_clock);
    rules_on_presence(&r, false, &s_clock);
    advance(&r, MIN(119));
    CHECK(count(ALERT_INACTIVITY, ALERT_ACTIVE) == 0);
    advance(&r, MIN(1));
    CHECK(count(ALERT_INACTIVITY, ALERT_ACTIVE) == 1);
    rules_on_presence(&r, true, &s_clock);
    CHECK(count(ALERT_INACTIVITY, ALERT_RESOLVED) == 1);

    /* At night, hours without motion are a resident asleep. */
    setup(&r, 22, 30);
    advance(&r, MIN(7 * 60));
    CHECK(count(ALERT_INACTIVITY, ALERT_ACTIVE) == 0);
}

static void test_no_pir_suspends_presence_rules(void){
    rules_t r;
    /* At night: the bedroom door opens and stays open, with no PIR to say anyone is inside. */
    setup(&r, 23, 0);
    rules_set_presence_fitted(&r, false);
    rules_on_door(&r, 0, true, &s_clock);
    advance(&r, MIN(30));
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 0);

    /* By day: hours without "presence" are only a sensor that is not there. */
    setup(&r, 9, 0);
    rules_set_presence_fitted(&r, false);
    advance(&r, MIN(180));
    CHECK(count(ALERT_INACTIVITY, ALERT_ACTIVE) == 0);

    /* The rules that do not need it still run: the bathroom door. */
    rules_on_door(&r, 1, true, &s_clock);
    rules_on_door(&r, 1, false, &s_clock);
    advance(&r, MIN(41));
    CHECK(count(ALERT_BATHROOM, ALERT_ACTIVE) == 1);
}

static void test_absent_door_never_alerts(void){
    rules_t r;
    /* A door that is not fitted never reports, not even its boot state: nothing can arm. */
    memset(&s_clock, 0, sizeof(s_clock));
    s_clock.mono_ms = 1000;
    clock_at(23, 0);
    s_event_count = 0;
    rules_params_t p;
    rules_default_params(&p);
    p.door_role[0] = DOOR_ROLE_EXIT;
    p.door_role[1] = DOOR_ROLE_BATHROOM;
    rules_init(&r, &p, DOOR_IDS, 2, record, NULL, &s_clock);
    rules_on_door(&r, 1, false, &s_clock);          /* only the bathroom is fitted */
    rules_on_presence(&r, false, &s_clock);
    advance(&r, MIN(60));
    CHECK(count(ALERT_NIGHT_EXIT, ALERT_ACTIVE) == 0);
    CHECK(s_event_count == 0);
}

static void test_inactivity_not_after_leaving(void){
    rules_t r;
    setup(&r, 9, 0);
    rules_on_presence(&r, true, &s_clock);
    rules_on_presence(&r, false, &s_clock);
    rules_on_door(&r, 0, true, &s_clock);           /* went out */
    advance(&r, MIN(180));
    CHECK(count(ALERT_INACTIVITY, ALERT_ACTIVE) == 0);
}

static void test_temperature_persistence_and_hysteresis(void){
    rules_t r;
    setup(&r, 12, 0);

    for (int i = 0; i < 19; i++) {                  /* 9.5 min hot, every 30 s */
        rules_on_temperature(&r, 29.0f, &s_clock);
        advance(&r, 30000);
    }
    CHECK(count(ALERT_TEMPERATURE, ALERT_ACTIVE) == 0);
    rules_on_temperature(&r, 29.0f, &s_clock);
    advance(&r, 30000);
    rules_on_temperature(&r, 29.0f, &s_clock);
    CHECK(count(ALERT_TEMPERATURE, ALERT_ACTIVE) == 1);
    CHECK(last()->temp_limit_c == 28.0f);

    /* 27.8 is inside the band but within the hysteresis: does not resolve. */
    for (int i = 0; i < 30; i++) {
        rules_on_temperature(&r, 27.8f, &s_clock);
        advance(&r, 30000);
    }
    CHECK(count(ALERT_TEMPERATURE, ALERT_RESOLVED) == 0);

    for (int i = 0; i < 21; i++) {
        rules_on_temperature(&r, 25.0f, &s_clock);
        advance(&r, 30000);
    }
    CHECK(count(ALERT_TEMPERATURE, ALERT_RESOLVED) == 1);
    CHECK(count(ALERT_TEMPERATURE, ALERT_ACTIVE) == 1);
}

static void test_temperature_brief_spike_ignored(void){
    rules_t r;
    setup(&r, 12, 0);
    for (int i = 0; i < 10; i++) {
        rules_on_temperature(&r, 31.0f, &s_clock);
        advance(&r, 30000);
    }
    rules_on_temperature(&r, 24.0f, &s_clock);
    for (int i = 0; i < 10; i++) {
        rules_on_temperature(&r, 31.0f, &s_clock);
        advance(&r, 30000);
    }
    CHECK(count(ALERT_TEMPERATURE, ALERT_ACTIVE) == 0);
}

static void test_fall_and_cancel(void){
    rules_t r;
    setup(&r, 3, 0);
    rules_on_fall(&r, "a1b2c3", 42, 120000, 7.88f, 0.97f, &s_clock);
    CHECK(count(ALERT_FALL, ALERT_ACTIVE) == 1);
    CHECK(strcmp(last()->id, "a1b2c3-42") == 0);
    CHECK(strcmp(last()->origin, "pulseira/a1b2c3") == 0);
    CHECK(last()->a2 == 7.88f);
    CHECK(last()->prob == 0.97f);

    rules_on_fall(&r, "a1b2c3", 42, 120000, 7.88f, 0.97f, &s_clock);   /* a resend: same seq, same uptime */
    CHECK(count(ALERT_FALL, ALERT_ACTIVE) == 1);

    rules_on_fall_cancel(&r, "a1b2c3", 41, &s_clock);   /* wrong seq */
    CHECK(count(ALERT_FALL, ALERT_CANCELLED) == 0);
    rules_on_fall_cancel(&r, "a1b2c3", 42, &s_clock);
    CHECK(count(ALERT_FALL, ALERT_CANCELLED) == 1);
    CHECK(strcmp(last()->id, "a1b2c3-42") == 0);
    rules_on_fall_cancel(&r, "a1b2c3", 42, &s_clock);   /* late duplicate */
    CHECK(count(ALERT_FALL, ALERT_CANCELLED) == 1);
}

static void test_new_fall_after_the_wearable_restarts(void){
    rules_t r;
    setup(&r, 3, 0);
    rules_on_fall(&r, "a1b2c3", 3, 95000, 7.88f, 0.97f, &s_clock);
    CHECK(count(ALERT_FALL, ALERT_ACTIVE) == 1);
    /* Nobody pressed "I'm fine"; the wearable restarts and falls again with the same seq. */
    rules_on_fall(&r, "a1b2c3", 3, 4200, 6.10f, 0.91f, &s_clock);
    CHECK(count(ALERT_FALL, ALERT_ACTIVE) == 2);
    CHECK(last()->a2 == 6.10f);
}

static void test_help_and_cancel(void){
    rules_t r;
    setup(&r, 3, 0);
    CHECK(!rules_any_active(&r));
    rules_on_help(&r, "a1b2c3", 7, 50000, &s_clock);
    CHECK(count(ALERT_HELP, ALERT_ACTIVE) == 1);
    CHECK(strcmp(last()->id, "a1b2c3-7") == 0);
    CHECK(strcmp(last()->origin, "pulseira/a1b2c3") == 0);
    CHECK(strcmp(rules_alert_type_name(last()->type), "ajuda") == 0);
    CHECK(rules_any_active(&r));

    rules_on_help(&r, "a1b2c3", 7, 50000, &s_clock);   /* a resend */
    CHECK(count(ALERT_HELP, ALERT_ACTIVE) == 1);

    rules_on_help_cancel(&r, "a1b2c3", 6, &s_clock);   /* wrong seq */
    CHECK(count(ALERT_HELP, ALERT_CANCELLED) == 0);
    rules_on_help_cancel(&r, "a1b2c3", 7, &s_clock);
    CHECK(count(ALERT_HELP, ALERT_CANCELLED) == 1);
    CHECK(strcmp(last()->id, "a1b2c3-7") == 0);
    CHECK(!rules_any_active(&r));
    rules_on_help_cancel(&r, "a1b2c3", 7, &s_clock);   /* late duplicate */
    CHECK(count(ALERT_HELP, ALERT_CANCELLED) == 1);

    /* The resident insists after the window: a new request with a new seq. */
    rules_on_help(&r, "a1b2c3", 9, 80000, &s_clock);
    CHECK(count(ALERT_HELP, ALERT_ACTIVE) == 2);
    CHECK(strcmp(last()->id, "a1b2c3-9") == 0);
}

static void test_help_after_the_wearable_restarts(void){
    rules_t r;
    setup(&r, 3, 0);
    rules_on_help(&r, "a1b2c3", 3, 95000, &s_clock);
    /* Same seq, another uptime: the wearable restarted, so this is a new request. */
    rules_on_help(&r, "a1b2c3", 3, 4200, &s_clock);
    CHECK(count(ALERT_HELP, ALERT_ACTIVE) == 2);
}

static void test_fall_and_help_are_separate(void){
    rules_t r;
    setup(&r, 3, 0);
    rules_on_help(&r, "a1b2c3", 10, 60000, &s_clock);
    rules_on_fall(&r, "a1b2c3", 11, 61000, 7.88f, 0.97f, &s_clock);
    CHECK(count(ALERT_HELP, ALERT_ACTIVE) == 1 && count(ALERT_FALL, ALERT_ACTIVE) == 1);

    /* A cancel names one alert by its seq; it never closes the other type. */
    rules_on_help_cancel(&r, "a1b2c3", 11, &s_clock);
    rules_on_fall_cancel(&r, "a1b2c3", 10, &s_clock);
    CHECK(count(ALERT_HELP, ALERT_CANCELLED) == 0 && count(ALERT_FALL, ALERT_CANCELLED) == 0);

    rules_on_fall_cancel(&r, "a1b2c3", 11, &s_clock);
    CHECK(count(ALERT_FALL, ALERT_CANCELLED) == 1);
    CHECK(count(ALERT_HELP, ALERT_CANCELLED) == 0);
    CHECK(rules_any_active(&r));   /* the help request is still open for the caregivers */
}

static void test_wearable_offline_and_back(void){
    rules_t r;
    setup(&r, 3, 0);
    rules_expect_wearable(&r, "a1b2c3", &s_clock);
    advance(&r, 89000);
    CHECK(count(ALERT_WEARABLE_OFFLINE, ALERT_ACTIVE) == 0);
    advance(&r, 1000);
    CHECK(count(ALERT_WEARABLE_OFFLINE, ALERT_ACTIVE) == 1);
    CHECK(rules_wearable_offline(&r, "a1b2c3"));
    advance(&r, MIN(5));
    CHECK(count(ALERT_WEARABLE_OFFLINE, ALERT_ACTIVE) == 1);
    rules_on_wearable_seen(&r, "a1b2c3", &s_clock);
    CHECK(count(ALERT_WEARABLE_OFFLINE, ALERT_RESOLVED) == 1);
    CHECK(!rules_wearable_offline(&r, "a1b2c3"));
}

static void test_low_battery_hysteresis(void){
    rules_t r;
    setup(&r, 3, 0);
    rules_on_battery(&r, "a1b2c3", 3600, &s_clock);
    CHECK(count(ALERT_LOW_BATTERY, ALERT_ACTIVE) == 0);
    rules_on_battery(&r, "a1b2c3", 3480, &s_clock);
    CHECK(count(ALERT_LOW_BATTERY, ALERT_ACTIVE) == 1);
    CHECK(last()->battery_mv == 3480);
    rules_on_battery(&r, "a1b2c3", 3550, &s_clock);   /* above threshold, inside hysteresis */
    CHECK(count(ALERT_LOW_BATTERY, ALERT_RESOLVED) == 0);
    rules_on_battery(&r, "a1b2c3", 3700, &s_clock);
    CHECK(count(ALERT_LOW_BATTERY, ALERT_RESOLVED) == 1);
}

static void test_episode_ids_without_clock(void){
    rules_t r;
    setup(&r, 10, 0);
    s_clock.time_valid = false;
    rules_on_door(&r, 1, true, &s_clock);
    rules_on_door(&r, 1, false, &s_clock);
    advance(&r, MIN(40));
    CHECK(count(ALERT_BATHROOM, ALERT_ACTIVE) == 1);
    CHECK(strncmp(last()->id, "banheiro_prolongado-m", 21) == 0);
}

int main(void){
    struct { const char *name; void (*fn)(void); } tests[] = {
        { "boot state is not an event",            test_boot_state_is_not_an_event },
        { "night exit fires and resolves",         test_night_exit_fires_and_resolves },
        { "night exit cleared by presence",        test_night_exit_cleared_by_presence_inside },
        { "night exit ignored by day",             test_night_exit_ignored_by_day },
        { "night exit needs wall clock",           test_night_exit_needs_wall_clock },
        { "bathroom fires and resolves",           test_bathroom_fires_and_resolves },
        { "bathroom cleared by bedroom presence",  test_bathroom_cleared_when_resident_is_in_bedroom },
        { "bathroom cleared when leaving",         test_bathroom_cleared_when_resident_leaves },
        { "inactivity by day only",                test_inactivity_by_day_only },
        { "inactivity not after leaving",          test_inactivity_not_after_leaving },
        { "no PIR suspends presence rules",        test_no_pir_suspends_presence_rules },
        { "absent door never alerts",              test_absent_door_never_alerts },
        { "temperature persistence/hysteresis",    test_temperature_persistence_and_hysteresis },
        { "temperature brief spike ignored",       test_temperature_brief_spike_ignored },
        { "fall and cancel",                       test_fall_and_cancel },
        { "new fall after the wearable restarts",  test_new_fall_after_the_wearable_restarts },
        { "help and cancel",                       test_help_and_cancel },
        { "help after the wearable restarts",      test_help_after_the_wearable_restarts },
        { "fall and help are separate",            test_fall_and_help_are_separate },
        { "wearable offline and back",             test_wearable_offline_and_back },
        { "low battery hysteresis",                test_low_battery_hysteresis },
        { "episode ids without clock",             test_episode_ids_without_clock },
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        int before = s_failures;
        tests[i].fn();
        printf("%s %s\n", s_failures == before ? "ok  " : "FAIL", tests[i].name);
    }
    printf("\n%d checks, %d failed\n", s_checks, s_failures);
    return s_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
