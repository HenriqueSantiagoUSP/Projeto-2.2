#pragma once

/*
 * Alert rules of the room hub (docs/arquitetura.md, section Regras).
 *
 * Pure C with no ESP-IDF dependency, so the same file runs on the hub and in the
 * host tests (test_host/). The engine never reads a clock: every entry point gets
 * the current time in a rules_clock_t, which is what lets the tests replay a whole
 * night in microseconds.
 *
 * Each alert is an episode: reported once as ALERT_ACTIVE and later once as
 * ALERT_RESOLVED or ALERT_CANCELLED, never repeated while its condition holds.
 * All calls must come from a single task; the engine has no locking.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RULES_MAX_DOORS          8
#define RULES_MAX_WEARABLES      4
#define RULES_WEARABLE_ID_LEN    7     /* "a1b2c3" + NUL */
#define RULES_DOOR_ID_LEN        16
#define RULES_ALERT_ID_LEN       40
#define RULES_ORIGIN_LEN         32

/*
 * Presence detected at least this long after a door event means someone is in the
 * bedroom. Earlier detections are the resident walking past the PIR on the way, or
 * the PIR still holding its output from before the door moved.
 */
#define RULES_PRESENCE_GRACE_MS      (60u * 1000u)

/* A low battery alert resolves only this far above the threshold, so it does not flap. */
#define RULES_BATTERY_HYSTERESIS_MV  100

typedef enum {
    DOOR_ROLE_OTHER,
    DOOR_ROLE_EXIT,       /* the bedroom door: arms the night exit rule */
    DOOR_ROLE_BATHROOM,   /* arms the bathroom rule */
} door_role_t;

typedef enum {
    ALERT_FALL,
    ALERT_NIGHT_EXIT,
    ALERT_BATHROOM,
    ALERT_INACTIVITY,
    ALERT_TEMPERATURE,
    ALERT_WEARABLE_OFFLINE,
    ALERT_LOW_BATTERY,
} alert_type_t;

typedef enum {
    ALERT_ACTIVE,
    ALERT_RESOLVED,
    ALERT_CANCELLED,
} alert_state_t;

/* Every tunable of the rules; the hub fills it from config/desejada (docs/protocolo.md §1). */
typedef struct {
    uint16_t    night_start_min;     /* minutes after local midnight; start > end wraps midnight */
    uint16_t    night_end_min;
    uint16_t    night_exit_min;
    uint16_t    bathroom_min;
    uint16_t    inactivity_min;
    float       temp_min_c;
    float       temp_max_c;
    float       temp_hysteresis_c;
    uint16_t    temp_persist_min;
    uint16_t    wearable_offline_s;
    uint16_t    low_battery_mv;
    door_role_t door_role[RULES_MAX_DOORS];
} rules_params_t;

typedef struct {
    uint64_t mono_ms;         /* monotonic time since boot */
    bool     time_valid;      /* the two wall clock fields below are meaningful */
    int64_t  epoch_s;         /* UTC seconds, only used to name alert episodes */
    uint16_t minute_of_day;   /* local time, for the night window */
} rules_clock_t;

/* One alert event. Which detail fields are meaningful depends on type (docs/protocolo.md §1). */
typedef struct {
    alert_type_t  type;
    alert_state_t state;
    char          id[RULES_ALERT_ID_LEN];
    char          origin[RULES_ORIGIN_LEN];
    float         a2;             /* fall: filtered |a|^2 at the trigger peak, g^2 */
    float         prob;           /* fall: the wearable classifier's P(fall), 0..1 */
    char          door_id[RULES_DOOR_ID_LEN];
    uint16_t      minutes;
    float         temp_c;
    float         temp_limit_c;
    uint16_t      battery_mv;
    uint32_t      last_contact_s;
    char          wearable[RULES_WEARABLE_ID_LEN];
} rules_alert_t;

typedef void (*rules_alert_cb_t)(const rules_alert_t *alert, void *ctx);

typedef struct {
    bool active;
    char id[RULES_ALERT_ID_LEN];
} rules_episode_t;

typedef struct {
    char            id[RULES_WEARABLE_ID_LEN];
    uint64_t        last_seen_ms;
    bool            fall_open;
    uint16_t        fall_seq;
    rules_episode_t fall;
    rules_episode_t offline;
    rules_episode_t low_battery;
} rules_wearable_t;

/* A timed rule that a door event arms and a later observation clears. */
typedef struct {
    bool            armed;
    uint64_t        armed_ms;
    uint8_t         door;
    rules_episode_t episode;
} rules_armed_t;

typedef struct {
    rules_params_t     params;
    const char *const *door_ids;
    size_t             door_count;
    rules_alert_cb_t   cb;
    void              *cb_ctx;

    bool               presence_fitted;  /* false: no PIR, the presence rules are suspended */
    bool               present;
    uint64_t           last_presence_ms;
    bool               door_known[RULES_MAX_DOORS];   /* a first state has been reported */
    bool               door_open[RULES_MAX_DOORS];
    bool               exit_opened_after_presence;

    rules_armed_t      night_exit;
    rules_armed_t      bathroom;
    rules_episode_t    inactivity;

    bool               temp_out_counting;
    uint64_t           temp_out_since_ms;
    bool               temp_in_counting;
    uint64_t           temp_in_since_ms;
    float              temp_limit_c;
    rules_episode_t    temperature;

    rules_wearable_t   wearables[RULES_MAX_WEARABLES];
    size_t             wearable_count;
} rules_t;

/* Values used until a configuration arrives (docs/arquitetura.md, Regras). */
void rules_default_params(rules_params_t *params);

/* door_ids must outlive the engine; door_count is clamped to RULES_MAX_DOORS. */
void rules_init(rules_t *rules, const rules_params_t *params,
                const char *const *door_ids, size_t door_count,
                rules_alert_cb_t cb, void *cb_ctx, const rules_clock_t *now);

/*
 * No presence sensor fitted (menuconfig): without it "nobody seen" means nothing, so
 * the rules built on it are suspended: inactivity, and night exit (door open AND no
 * presence inside). Call right after rules_init(). Fitted by default.
 */
void rules_set_presence_fitted(rules_t *rules, bool fitted);

/* Replaces the parameters; running episodes keep their state. */
void rules_set_params(rules_t *rules, const rules_params_t *params);

void rules_on_presence(rules_t *rules, bool detected, const rules_clock_t *now);
void rules_on_door(rules_t *rules, size_t door, bool open, const rules_clock_t *now);
void rules_on_temperature(rules_t *rules, float celsius, const rules_clock_t *now);

/* A wearable the hub expects even before it is heard, so silence from boot counts. */
void rules_expect_wearable(rules_t *rules, const char *wearable, const rules_clock_t *now);
/* Any message from a wearable. */
void rules_on_wearable_seen(rules_t *rules, const char *wearable, const rules_clock_t *now);
void rules_on_battery(rules_t *rules, const char *wearable, uint16_t battery_mv, const rules_clock_t *now);
void rules_on_fall(rules_t *rules, const char *wearable, uint16_t seq, float a2, float prob,
                   const rules_clock_t *now);
void rules_on_fall_cancel(rules_t *rules, const char *wearable, uint16_t fall_seq, const rules_clock_t *now);

/* Evaluates the timed rules; call at least once per second. */
void rules_tick(rules_t *rules, const rules_clock_t *now);

/* True when the wearable is currently considered offline. */
bool rules_wearable_offline(const rules_t *rules, const char *wearable);

/*
 * True while any episode is open. A fall stays open until the resident cancels it:
 * nothing on the hub can tell that help arrived (caregiver acknowledgement is future work).
 */
bool rules_any_active(const rules_t *rules);

/* The "tipo" and "estado" strings of the MQTT contract. */
const char *rules_alert_type_name(alert_type_t type);
const char *rules_alert_state_name(alert_state_t state);
