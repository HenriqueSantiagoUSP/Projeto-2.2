#include <stdio.h>
#include <string.h>

#include "rules.h"
#include "protocol_mqtt.h"

#define MIN_TO_MS(m)   ((uint64_t) (m) * 60u * 1000u)

void rules_default_params(rules_params_t *params){
    memset(params, 0, sizeof(*params));
    params->night_start_min    = 22 * 60;
    params->night_end_min      = 6 * 60;
    params->night_exit_min     = 15;
    params->bathroom_min       = 40;
    params->inactivity_min     = 120;
    params->temp_min_c         = 18.0f;
    params->temp_max_c         = 28.0f;
    params->temp_hysteresis_c  = 0.5f;
    params->temp_persist_min   = 10;
    params->wearable_offline_s = 90;
    params->low_battery_mv     = 3500;
    /* Door roles stay DOOR_ROLE_OTHER: the board decides which door is which. */
}

const char *rules_alert_type_name(alert_type_t type){
    switch (type) {
        case ALERT_FALL:             return MQTT_ALERT_FALL;
        case ALERT_HELP:             return MQTT_ALERT_HELP;
        case ALERT_NIGHT_EXIT:      return MQTT_ALERT_NIGHT_EXIT;
        case ALERT_BATHROOM:         return MQTT_ALERT_BATHROOM;
        case ALERT_INACTIVITY:       return MQTT_ALERT_INACTIVITY;
        case ALERT_TEMPERATURE:      return MQTT_ALERT_TEMPERATURE;
        case ALERT_WEARABLE_OFFLINE: return MQTT_ALERT_WEARABLE_OFF;
        case ALERT_LOW_BATTERY:      return MQTT_ALERT_LOW_BATTERY;
    }
    return "?";
}

const char *rules_alert_state_name(alert_state_t state){
    switch (state) {
        case ALERT_ACTIVE:    return MQTT_ALERT_ACTIVE;
        case ALERT_RESOLVED:  return MQTT_ALERT_RESOLVED;
        case ALERT_CANCELLED: return MQTT_ALERT_CANCELLED;
    }
    return "?";
}

static bool is_night(const rules_params_t *params, const rules_clock_t *now){
    if (!now->time_valid || params->night_start_min == params->night_end_min) {
        return false;
    }
    uint16_t m = now->minute_of_day;
    if (params->night_start_min < params->night_end_min) {
        return m >= params->night_start_min && m < params->night_end_min;
    }
    return m >= params->night_start_min || m < params->night_end_min;   /* wraps midnight */
}

/* Names an episode after its type and start time, so ACTIVE and its closing event match. */
static void make_episode_id(char *id, const char *prefix, const rules_clock_t *now){
    if (now->time_valid) {
        snprintf(id, RULES_ALERT_ID_LEN, "%s-%lld", prefix, (long long) now->epoch_s);
    } else {
        /* No wall clock yet: uptime keeps it unique until the next reboot. */
        snprintf(id, RULES_ALERT_ID_LEN, "%s-m%llu", prefix, (unsigned long long) (now->mono_ms / 1000));
    }
}

static void alert_template(rules_alert_t *alert, alert_type_t type, const char *origin){
    memset(alert, 0, sizeof(*alert));
    alert->type = type;
    snprintf(alert->origin, sizeof(alert->origin), "%s", origin);
}

static void emit(rules_t *rules, rules_episode_t *episode, rules_alert_t *alert, alert_state_t state){
    alert->state = state;
    snprintf(alert->id, sizeof(alert->id), "%s", episode->id);
    episode->active = (state == ALERT_ACTIVE);
    if (rules->cb != NULL) {
        rules->cb(alert, rules->cb_ctx);
    }
}

static void door_origin(const rules_t *rules, size_t door, char *out, size_t out_len){
    snprintf(out, out_len, "%s/%s", MQTT_TOPIC_DOOR, rules->door_ids[door]);
}

static void wearable_origin(const char *wearable, char *out, size_t out_len){
    snprintf(out, out_len, "%s/%s", MQTT_TOPIC_WEARABLE, wearable);
}

/* ---------------------------------------------------------------------------
 * Door armed rules (night exit, bathroom)
 * ------------------------------------------------------------------------- */

static void armed_alert(const rules_t *rules, const rules_armed_t *armed, alert_type_t type,
                        uint16_t minutes, rules_alert_t *alert){
    char origin[RULES_ORIGIN_LEN];
    door_origin(rules, armed->door, origin, sizeof(origin));
    alert_template(alert, type, origin);
    snprintf(alert->door_id, sizeof(alert->door_id), "%s", rules->door_ids[armed->door]);
    alert->minutes = minutes;
}

static void armed_arm(rules_armed_t *armed, size_t door, const rules_clock_t *now){
    armed->armed = true;
    armed->armed_ms = now->mono_ms;
    armed->door = (uint8_t) door;
}

/* Clears an armed rule, closing its episode as resolved if it had fired. */
static void armed_clear(rules_t *rules, rules_armed_t *armed, alert_type_t type, uint16_t minutes){
    if (!armed->armed) {
        return;
    }
    armed->armed = false;
    if (armed->episode.active) {
        rules_alert_t alert;
        armed_alert(rules, armed, type, minutes, &alert);
        emit(rules, &armed->episode, &alert, ALERT_RESOLVED);
    }
}

static void armed_check(rules_t *rules, rules_armed_t *armed, alert_type_t type,
                        uint16_t minutes, const rules_clock_t *now){
    if (!armed->armed || armed->episode.active) {
        return;
    }
    if (now->mono_ms - armed->armed_ms < MIN_TO_MS(minutes)) {
        return;
    }
    rules_alert_t alert;
    armed_alert(rules, armed, type, minutes, &alert);
    make_episode_id(armed->episode.id, rules_alert_type_name(type), now);
    emit(rules, &armed->episode, &alert, ALERT_ACTIVE);
}

/* Presence past the grace period proves someone is in the bedroom. */
static void presence_seen_inside(rules_t *rules, const rules_clock_t *now){
    const rules_params_t *p = &rules->params;

    if (rules->night_exit.armed && now->mono_ms >= rules->night_exit.armed_ms + RULES_PRESENCE_GRACE_MS) {
        armed_clear(rules, &rules->night_exit, ALERT_NIGHT_EXIT, p->night_exit_min);
    }
    if (rules->bathroom.armed && now->mono_ms >= rules->bathroom.armed_ms + RULES_PRESENCE_GRACE_MS) {
        armed_clear(rules, &rules->bathroom, ALERT_BATHROOM, p->bathroom_min);
    }
}

/* ---------------------------------------------------------------------------
 * Wearables
 * ------------------------------------------------------------------------- */

static rules_wearable_t *wearable_find(rules_t *rules, const char *id, bool create, const rules_clock_t *now){
    for (size_t i = 0; i < rules->wearable_count; i++) {
        if (strcmp(rules->wearables[i].id, id) == 0) {
            return &rules->wearables[i];
        }
    }
    if (!create || rules->wearable_count >= RULES_MAX_WEARABLES) {
        return NULL;
    }
    rules_wearable_t *w = &rules->wearables[rules->wearable_count++];
    memset(w, 0, sizeof(*w));
    snprintf(w->id, sizeof(w->id), "%s", id);
    w->last_seen_ms = now->mono_ms;
    return w;
}

static void wearable_alert(const rules_wearable_t *w, alert_type_t type, rules_alert_t *alert){
    char origin[RULES_ORIGIN_LEN];
    wearable_origin(w->id, origin, sizeof(origin));
    alert_template(alert, type, origin);
    snprintf(alert->wearable, sizeof(alert->wearable), "%s", w->id);
}

/* The same message again: a resend (same seq and same sender uptime) that reached us after another one. */
static bool resident_alert_repeated(const rules_wearable_alert_t *a, uint16_t seq, uint32_t uptime_ms){
    return a->episode.active && a->seq == seq && a->uptime_ms == uptime_ms;
}

/*
 * Opens a fall or a help request. A new one supersedes an open one of the same type. Only
 * the resident (a cancel) or, later, a caregiver closes it; the hub never resolves it on its own.
 */
static void resident_alert_open(rules_t *rules, const rules_wearable_t *w, rules_wearable_alert_t *a,
                                rules_alert_t *alert, uint16_t seq, uint32_t uptime_ms){
    a->seq = seq;
    a->uptime_ms = uptime_ms;
    snprintf(a->episode.id, sizeof(a->episode.id), "%s-%u", w->id, (unsigned) seq);
    emit(rules, &a->episode, alert, ALERT_ACTIVE);
}

static void resident_alert_cancel(rules_t *rules, const rules_wearable_t *w, rules_wearable_alert_t *a,
                                  alert_type_t type, uint16_t seq){
    if (!a->episode.active || a->seq != seq) {
        return;   /* nothing open under that seq: a late duplicate */
    }
    rules_alert_t alert;
    wearable_alert(w, type, &alert);
    emit(rules, &a->episode, &alert, ALERT_CANCELLED);
}

/* ---------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void rules_init(rules_t *rules, const rules_params_t *params,
                const char *const *door_ids, size_t door_count,
                rules_alert_cb_t cb, void *cb_ctx, const rules_clock_t *now){
    memset(rules, 0, sizeof(*rules));
    rules->params = *params;
    rules->door_ids = door_ids;
    rules->door_count = door_count > RULES_MAX_DOORS ? RULES_MAX_DOORS : door_count;
    rules->cb = cb;
    rules->cb_ctx = cb_ctx;
    rules->presence_fitted = true;
    /* Nobody has been seen yet: inactivity counts from boot. */
    rules->last_presence_ms = now->mono_ms;
}

void rules_set_presence_fitted(rules_t *rules, bool fitted){
    rules->presence_fitted = fitted;
    if (!fitted) {
        rules->night_exit.armed = false;
    }
}

void rules_set_params(rules_t *rules, const rules_params_t *params){
    rules->params = *params;
}

void rules_on_presence(rules_t *rules, bool detected, const rules_clock_t *now){
    rules->present = detected;
    /* Both edges are a sighting: a falling edge means the room was busy until now. */
    rules->last_presence_ms = now->mono_ms;

    if (!detected) {
        return;
    }

    rules->exit_opened_after_presence = false;
    if (rules->inactivity.active) {
        rules_alert_t alert;
        alert_template(&alert, ALERT_INACTIVITY, MQTT_TOPIC_PRESENCE);
        alert.minutes = rules->params.inactivity_min;
        emit(rules, &rules->inactivity, &alert, ALERT_RESOLVED);
    }
    presence_seen_inside(rules, now);
}

void rules_on_door(rules_t *rules, size_t door, bool open, const rules_clock_t *now){
    if (door >= rules->door_count) {
        return;
    }

    bool known = rules->door_known[door];
    bool was_open = rules->door_open[door];
    rules->door_known[door] = true;
    rules->door_open[door] = open;

    /* The first report after boot is the starting state, not a movement. */
    if (!known || was_open == open) {
        return;
    }

    const rules_params_t *p = &rules->params;
    switch (p->door_role[door]) {
        case DOOR_ROLE_EXIT:
            if (!open) {
                break;
            }
            rules->exit_opened_after_presence = true;
            /* Re-arming restarts the wait, unless the alert already fired. Needs the PIR to clear it. */
            if (rules->presence_fitted && is_night(p, now) && !rules->night_exit.episode.active) {
                armed_arm(&rules->night_exit, door, now);
            }
            /* Leaving the bedroom means nobody is shut in the bathroom. */
            if (rules->bathroom.armed && !rules->bathroom.episode.active) {
                rules->bathroom.armed = false;
            }
            break;

        case DOOR_ROLE_BATHROOM:
            if (open) {
                armed_clear(rules, &rules->bathroom, ALERT_BATHROOM, p->bathroom_min);
            } else if (!rules->bathroom.episode.active) {
                armed_arm(&rules->bathroom, door, now);
            }
            break;

        case DOOR_ROLE_OTHER:
            break;
    }
}

void rules_on_temperature(rules_t *rules, float celsius, const rules_clock_t *now){
    const rules_params_t *p = &rules->params;
    uint64_t persist_ms = MIN_TO_MS(p->temp_persist_min);
    rules_alert_t alert;

    if (!rules->temperature.active) {
        bool out = celsius < p->temp_min_c || celsius > p->temp_max_c;
        if (!out) {
            rules->temp_out_counting = false;
            return;
        }
        if (!rules->temp_out_counting) {
            rules->temp_out_counting = true;
            rules->temp_out_since_ms = now->mono_ms;
        }
        if (now->mono_ms - rules->temp_out_since_ms < persist_ms) {
            return;
        }
        rules->temp_out_counting = false;
        rules->temp_in_counting = false;
        rules->temp_limit_c = celsius < p->temp_min_c ? p->temp_min_c : p->temp_max_c;
        alert_template(&alert, ALERT_TEMPERATURE, MQTT_TOPIC_TEMPERATURE);
        alert.temp_c = celsius;
        alert.temp_limit_c = rules->temp_limit_c;
        make_episode_id(rules->temperature.id, MQTT_ALERT_TEMPERATURE, now);
        emit(rules, &rules->temperature, &alert, ALERT_ACTIVE);
        return;
    }

    /* Back inside the band by the hysteresis margin, for as long as it took to fire. */
    bool in = celsius >= p->temp_min_c + p->temp_hysteresis_c &&
              celsius <= p->temp_max_c - p->temp_hysteresis_c;
    if (!in) {
        rules->temp_in_counting = false;
        return;
    }
    if (!rules->temp_in_counting) {
        rules->temp_in_counting = true;
        rules->temp_in_since_ms = now->mono_ms;
    }
    if (now->mono_ms - rules->temp_in_since_ms < persist_ms) {
        return;
    }
    rules->temp_in_counting = false;
    alert_template(&alert, ALERT_TEMPERATURE, MQTT_TOPIC_TEMPERATURE);
    alert.temp_c = celsius;
    alert.temp_limit_c = rules->temp_limit_c;
    emit(rules, &rules->temperature, &alert, ALERT_RESOLVED);
}

void rules_expect_wearable(rules_t *rules, const char *wearable, const rules_clock_t *now){
    wearable_find(rules, wearable, true, now);
}

void rules_on_wearable_seen(rules_t *rules, const char *wearable, const rules_clock_t *now){
    rules_wearable_t *w = wearable_find(rules, wearable, true, now);
    if (w == NULL) {
        return;
    }
    uint64_t silent_ms = now->mono_ms - w->last_seen_ms;
    w->last_seen_ms = now->mono_ms;

    if (w->offline.active) {
        rules_alert_t alert;
        wearable_alert(w, ALERT_WEARABLE_OFFLINE, &alert);
        alert.last_contact_s = (uint32_t) (silent_ms / 1000);
        emit(rules, &w->offline, &alert, ALERT_RESOLVED);
    }
}

void rules_on_battery(rules_t *rules, const char *wearable, uint16_t battery_mv, const rules_clock_t *now){
    rules_wearable_t *w = wearable_find(rules, wearable, true, now);
    if (w == NULL) {
        return;
    }
    const rules_params_t *p = &rules->params;
    rules_alert_t alert;

    if (!w->low_battery.active && battery_mv < p->low_battery_mv) {
        wearable_alert(w, ALERT_LOW_BATTERY, &alert);
        alert.battery_mv = battery_mv;
        char prefix[RULES_ALERT_ID_LEN];
        snprintf(prefix, sizeof(prefix), "%s-%s", MQTT_ALERT_LOW_BATTERY, w->id);
        make_episode_id(w->low_battery.id, prefix, now);
        emit(rules, &w->low_battery, &alert, ALERT_ACTIVE);
    } else if (w->low_battery.active && battery_mv > p->low_battery_mv + RULES_BATTERY_HYSTERESIS_MV) {
        wearable_alert(w, ALERT_LOW_BATTERY, &alert);
        alert.battery_mv = battery_mv;
        emit(rules, &w->low_battery, &alert, ALERT_RESOLVED);
    }
}

void rules_on_fall(rules_t *rules, const char *wearable, uint16_t seq, uint32_t uptime_ms, float a2, float prob,
                   const rules_clock_t *now){
    rules_wearable_t *w = wearable_find(rules, wearable, true, now);
    if (w == NULL) {
        return;
    }
    if (resident_alert_repeated(&w->fall, seq, uptime_ms)) {
        return;
    }
    rules_alert_t alert;
    wearable_alert(w, ALERT_FALL, &alert);
    alert.a2 = a2;
    alert.prob = prob;
    resident_alert_open(rules, w, &w->fall, &alert, seq, uptime_ms);
}

void rules_on_fall_cancel(rules_t *rules, const char *wearable, uint16_t fall_seq, const rules_clock_t *now){
    rules_wearable_t *w = wearable_find(rules, wearable, true, now);
    if (w != NULL) {
        resident_alert_cancel(rules, w, &w->fall, ALERT_FALL, fall_seq);
    }
}

void rules_on_help(rules_t *rules, const char *wearable, uint16_t seq, uint32_t uptime_ms, const rules_clock_t *now){
    rules_wearable_t *w = wearable_find(rules, wearable, true, now);
    if (w == NULL || resident_alert_repeated(&w->help, seq, uptime_ms)) {
        return;
    }
    rules_alert_t alert;
    wearable_alert(w, ALERT_HELP, &alert);
    resident_alert_open(rules, w, &w->help, &alert, seq, uptime_ms);
}

void rules_on_help_cancel(rules_t *rules, const char *wearable, uint16_t help_seq, const rules_clock_t *now){
    rules_wearable_t *w = wearable_find(rules, wearable, true, now);
    if (w != NULL) {
        resident_alert_cancel(rules, w, &w->help, ALERT_HELP, help_seq);
    }
}

void rules_tick(rules_t *rules, const rules_clock_t *now){
    const rules_params_t *p = &rules->params;

    /* The PIR holds its output while motion continues, so presence can outlast the grace. */
    if (rules->present) {
        presence_seen_inside(rules, now);
    }

    armed_check(rules, &rules->night_exit, ALERT_NIGHT_EXIT, p->night_exit_min, now);
    armed_check(rules, &rules->bathroom, ALERT_BATHROOM, p->bathroom_min, now);

    /* At night a sleeping resident does not move, so inactivity only counts by day. */
    if (rules->presence_fitted && !rules->inactivity.active && !rules->present && now->time_valid &&
        !is_night(p, now) &&
        !rules->exit_opened_after_presence &&
        now->mono_ms - rules->last_presence_ms >= MIN_TO_MS(p->inactivity_min)) {
        rules_alert_t alert;
        alert_template(&alert, ALERT_INACTIVITY, MQTT_TOPIC_PRESENCE);
        alert.minutes = p->inactivity_min;
        make_episode_id(rules->inactivity.id, MQTT_ALERT_INACTIVITY, now);
        emit(rules, &rules->inactivity, &alert, ALERT_ACTIVE);
    }

    for (size_t i = 0; i < rules->wearable_count; i++) {
        rules_wearable_t *w = &rules->wearables[i];
        uint64_t silent_ms = now->mono_ms - w->last_seen_ms;
        if (w->offline.active || silent_ms < (uint64_t) p->wearable_offline_s * 1000u) {
            continue;
        }
        rules_alert_t alert;
        wearable_alert(w, ALERT_WEARABLE_OFFLINE, &alert);
        alert.last_contact_s = (uint32_t) (silent_ms / 1000);
        char prefix[RULES_ALERT_ID_LEN];
        snprintf(prefix, sizeof(prefix), "%s-%s", MQTT_ALERT_WEARABLE_OFF, w->id);
        make_episode_id(w->offline.id, prefix, now);
        emit(rules, &w->offline, &alert, ALERT_ACTIVE);
    }
}

bool rules_any_active(const rules_t *rules){
    if (rules->night_exit.episode.active || rules->bathroom.episode.active ||
        rules->inactivity.active || rules->temperature.active) {
        return true;
    }
    for (size_t i = 0; i < rules->wearable_count; i++) {
        const rules_wearable_t *w = &rules->wearables[i];
        if (w->fall.episode.active || w->help.episode.active || w->offline.active || w->low_battery.active) {
            return true;
        }
    }
    return false;
}

bool rules_resident_alert_active(const rules_t *rules){
    for (size_t i = 0; i < rules->wearable_count; i++) {
        const rules_wearable_t *w = &rules->wearables[i];
        if (w->fall.episode.active || w->help.episode.active) {
            return true;
        }
    }
    return false;
}

bool rules_wearable_offline(const rules_t *rules, const char *wearable){
    for (size_t i = 0; i < rules->wearable_count; i++) {
        if (strcmp(rules->wearables[i].id, wearable) == 0) {
            return rules->wearables[i].offline.active;
        }
    }
    return false;
}
