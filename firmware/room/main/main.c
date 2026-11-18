#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "board_pins.h"
#include "can.h"
#include "can_ids.h"
#include "espnow_link.h"
#include "hcsr501.h"
#include "hub_config.h"
#include "indicators.h"
#include "lm35.h"
#include "mqtt_link.h"
#include "net.h"
#include "protocol_can.h"
#include "protocol_mqtt.h"
#include "reedswitch.h"
#include "report.h"
#include "rtos_err.h"
#include "rules.h"
#include "sensor_events.h"
#include "sys_events.h"
#include "task_config.h"

static const char *TAG = "main";

#define HUB_QUEUE_DEPTH     32
#define HUB_TICK_MS         1000    /* rules run at least this often */
#define FALL_BUZZER_MS      60000   /* local alarm on a fall or a help request; the cancel stops it earlier */
#define JSON_MAX            384
#define REPORT_MAX_MS       ((uint64_t) MQTT_REPORT_MAX_INTERVAL_S * 1000u)
/* The sign of life of each periodic reading, due on the reading that falls on the 5th minute. */
#define TEMP_REPORT_MAX_MS      REPORT_MAX_MS_FOR_PERIOD(REPORT_MAX_MS, LM35_PERIOD_MS)
#define WEARABLE_REPORT_MAX_MS  REPORT_MAX_MS_FOR_PERIOD(REPORT_MAX_MS, ESPNOW_HEARTBEAT_PERIOD_MS)

/* Every door needs a CAN nibble of its own (docs/protocolo.md §2). */
#define DOOR_ONE(index, gpio, id, role)   + 1
_Static_assert((0 BOARD_DOORS(DOOR_ONE)) <= CAN_DOORS_MAX, "more doors in BOARD_DOORS than CAN nibbles");
#undef DOOR_ONE

/*
 * Every producer (sensors, ESP-NOW, MQTT, network) only queues a hub_event_t;
 * hub_task alone runs the rules and publishes. That is what lets the rule engine
 * go without locks.
 */
typedef enum {
    HUB_EV_SENSOR,
    HUB_EV_ESPNOW,
    HUB_EV_CONFIG,
    HUB_EV_NET,
} hub_event_kind_t;

typedef struct {
    hub_event_kind_t kind;
    union {
        sensor_reading_t reading;
        espnow_rx_msg_t  espnow;
        struct { char *json; size_t len; } config;   /* malloc'd; hub_task frees it */
        bool net_up;
    };
} hub_event_t;

/* Last state of every input, republished on each MQTT (re)connection. */
typedef struct {
    bool     presence_known;
    bool     presence;
    bool     door_known[RULES_MAX_DOORS];
    bool     door_open[RULES_MAX_DOORS];
    bool     temp_known;
    float    temp_c;        /* the temperature last published (MQTT and CAN), not the last reading */
    report_t temp_report;   /* every reading goes to the rules; only a change goes out */
} hub_state_t;

typedef struct {
    bool         used;
    bool         heard_heartbeat;
    char         id[ESPNOW_WEARABLE_ID_LEN];
    uint16_t     battery_mv;
    uint16_t     activity_mg;    /* as last published: the RMS of the heartbeats it summed up */
    int8_t       rssi;
    /* pulseira/<id> goes out on the first heartbeat, then as a summary every few minutes. */
    report_t     report;
    report_rms_t activity;       /* the heartbeats' activity since the last publish */
} wearable_cache_t;

static QueueHandle_t s_queue = NULL;
static rules_t s_rules;
static rules_params_t s_params;
static hub_config_origin_t s_config_origin = HUB_CONFIG_DEFAULT;
static const char *const *s_door_ids = NULL;
static size_t s_door_count = 0;
static hub_state_t s_state;
static wearable_cache_t s_wearables[RULES_MAX_WEARABLES];
static bool s_reported_time_valid = false;
static atomic_bool s_mqtt_up_pending = false;   /* set by the MQTT task, taken by hub_task */

/* ---------------------------------------------------------------------------
 * Time
 * ------------------------------------------------------------------------- */

static void hub_clock(rules_clock_t *clock){
    clock->mono_ms = (uint64_t) (esp_timer_get_time() / 1000);
    clock->time_valid = net_time_valid();

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);
    clock->epoch_s = (int64_t) now;
    clock->minute_of_day = (uint16_t) (local.tm_hour * 60 + local.tm_min);
}

/* ",\"ts\":<epoch ms>", or nothing while the clock is not set (docs/protocolo.md §1). */
static void ts_field(char *out, size_t out_len){
    if (!net_time_valid()) {
        out[0] = '\0';
        return;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(out, out_len, ",\"ts\":%lld", (long long) tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

/* ---------------------------------------------------------------------------
 * Publishing (docs/protocolo.md §1)
 * ------------------------------------------------------------------------- */

static void publish_status(void){
    char ts[32], json[JSON_MAX];
    ts_field(ts, sizeof(ts));
    snprintf(json, sizeof(json), "{\"v\":%d,\"estado\":\"%s\",\"fw\":\"%s\",\"hora\":%s%s}",
             MQTT_JSON_VERSION, MQTT_STATE_ONLINE, esp_app_get_description()->version,
             net_time_valid() ? "true" : "false", ts);
    mqtt_link_publish(MQTT_TOPIC_STATUS, json, 1, true);
}

static void publish_presence(bool detected){
    char ts[32], json[JSON_MAX];
    ts_field(ts, sizeof(ts));
    snprintf(json, sizeof(json), "{\"v\":%d,\"sensor\":\"HC-SR501\",\"estado\":\"%s\"%s}",
             MQTT_JSON_VERSION, detected ? MQTT_STATE_DETECTED : MQTT_STATE_CLEAR, ts);
    mqtt_link_publish(MQTT_TOPIC_PRESENCE, json, 1, true);
}

static void publish_door(size_t door, bool open){
    char ts[32], json[JSON_MAX], suffix[48];
    ts_field(ts, sizeof(ts));
    snprintf(suffix, sizeof(suffix), "%s/%s", MQTT_TOPIC_DOOR, s_door_ids[door]);
    snprintf(json, sizeof(json), "{\"v\":%d,\"sensor\":\"MC-38\",\"estado\":\"%s\"%s}",
             MQTT_JSON_VERSION, open ? MQTT_STATE_OPEN : MQTT_STATE_CLOSED, ts);
    mqtt_link_publish(suffix, json, 1, true);
}

static void publish_temperature(float celsius){
    char ts[32], json[JSON_MAX];
    ts_field(ts, sizeof(ts));
    snprintf(json, sizeof(json), "{\"v\":%d,\"sensor\":\"LM35DZ\",\"valor\":%.2f%s}",
             MQTT_JSON_VERSION, celsius, ts);
    mqtt_link_publish(MQTT_TOPIC_TEMPERATURE, json, 0, true);
}

static void publish_wearable(const wearable_cache_t *w, bool online){
    char ts[32], json[JSON_MAX], suffix[48];
    ts_field(ts, sizeof(ts));
    snprintf(suffix, sizeof(suffix), "%s/%s", MQTT_TOPIC_WEARABLE, w->id);

    if (!online) {
        snprintf(json, sizeof(json), "{\"v\":%d,\"estado\":\"%s\"%s}",
                 MQTT_JSON_VERSION, MQTT_STATE_OFFLINE, ts);
    } else if (w->heard_heartbeat && w->battery_mv != ESPNOW_BATTERY_UNKNOWN) {
        snprintf(json, sizeof(json),
                 "{\"v\":%d,\"estado\":\"%s\",\"bateria_mV\":%u,\"rssi\":%d,\"atividade\":%.3f%s}",
                 MQTT_JSON_VERSION, MQTT_STATE_ONLINE, w->battery_mv, w->rssi,
                 w->activity_mg / 1000.0f, ts);
    } else if (w->heard_heartbeat) {
        snprintf(json, sizeof(json), "{\"v\":%d,\"estado\":\"%s\",\"rssi\":%d,\"atividade\":%.3f%s}",
                 MQTT_JSON_VERSION, MQTT_STATE_ONLINE, w->rssi, w->activity_mg / 1000.0f, ts);
    } else {
        snprintf(json, sizeof(json), "{\"v\":%d,\"estado\":\"%s\",\"rssi\":%d%s}",
                 MQTT_JSON_VERSION, MQTT_STATE_ONLINE, w->rssi, ts);
    }
    mqtt_link_publish(suffix, json, 1, true);
}

static void publish_alert(const rules_alert_t *alert){
    char detail[160] = "";

    switch (alert->type) {
        case ALERT_FALL:
            if (alert->state == ALERT_ACTIVE) {
                /*
                 * The values come over the air. A NaN or Inf would print as "nan"/"inf", which is not
                 * JSON: the central would drop the whole alert. A fall is still a fall: leave the bad
                 * field out instead.
                 */
                bool a2_ok = isfinite(alert->a2) && alert->a2 >= 0.0f;
                bool prob_ok = isfinite(alert->prob) && alert->prob >= 0.0f && alert->prob <= 1.0f;
                int used = 0;
                if (a2_ok) {
                    used = snprintf(detail, sizeof(detail), "\"a2\":%.2f", alert->a2);
                }
                if (prob_ok && used >= 0 && (size_t) used < sizeof(detail)) {
                    snprintf(detail + used, sizeof(detail) - (size_t) used, "%s\"prob\":%.2f", a2_ok ? "," : "",
                             alert->prob);
                }
            }
            break;
        case ALERT_HELP:
            break;   /* the id and the origin say who asks: no detail */
        case ALERT_NIGHT_EXIT:
            snprintf(detail, sizeof(detail), "\"porta\":\"%s\",\"min_sem_presenca\":%u",
                     alert->door_id, alert->minutes);
            break;
        case ALERT_BATHROOM:
            snprintf(detail, sizeof(detail), "\"porta\":\"%s\",\"min\":%u", alert->door_id, alert->minutes);
            break;
        case ALERT_INACTIVITY:
            snprintf(detail, sizeof(detail), "\"min\":%u", alert->minutes);
            break;
        case ALERT_TEMPERATURE:
            snprintf(detail, sizeof(detail), "\"valor\":%.2f,\"limite\":%.1f", alert->temp_c, alert->temp_limit_c);
            break;
        case ALERT_WEARABLE_OFFLINE:
            snprintf(detail, sizeof(detail), "\"pulseira\":\"%s\",\"ultimo_contato_s\":%lu",
                     alert->wearable, (unsigned long) alert->last_contact_s);
            break;
        case ALERT_LOW_BATTERY:
            snprintf(detail, sizeof(detail), "\"pulseira\":\"%s\",\"bateria_mV\":%u",
                     alert->wearable, alert->battery_mv);
            break;
    }

    char ts[32], json[JSON_MAX];
    ts_field(ts, sizeof(ts));
    snprintf(json, sizeof(json),
             "{\"v\":%d,\"id\":\"%s\",\"tipo\":\"%s\",\"estado\":\"%s\",\"origem\":\"%s\",\"detalhe\":{%s}%s}",
             MQTT_JSON_VERSION, alert->id, rules_alert_type_name(alert->type),
             rules_alert_state_name(alert->state), alert->origin, detail, ts);
    /* Not retained: a dashboard opened hours later must not ring an old alarm. */
    mqtt_link_publish(MQTT_TOPIC_ALERT, json, 1, false);
}

static void publish_config_applied(const char *error){
    char *json = hub_config_to_json(&s_params, s_door_ids, s_door_count, s_config_origin, error);
    if (json == NULL) {
        ESP_LOGE(TAG, "out of memory serializing the configuration");
        return;
    }
    mqtt_link_publish(MQTT_TOPIC_CONFIG_APPLIED, json, 1, true);
    free(json);
}

/* ---------------------------------------------------------------------------
 * Wearables seen by this hub
 * ------------------------------------------------------------------------- */

static wearable_cache_t *wearable_cache(const char *id){
    wearable_cache_t *free_slot = NULL;
    for (size_t i = 0; i < RULES_MAX_WEARABLES; i++) {
        if (s_wearables[i].used && strcmp(s_wearables[i].id, id) == 0) {
            return &s_wearables[i];
        }
        if (free_slot == NULL && !s_wearables[i].used) {
            free_slot = &s_wearables[i];
        }
    }
    if (free_slot != NULL) {
        memset(free_slot, 0, sizeof(*free_slot));
        free_slot->used = true;
        snprintf(free_slot->id, sizeof(free_slot->id), "%s", id);
    }
    return free_slot;
}

/* ---------------------------------------------------------------------------
 * Event handling (hub_task only)
 * ------------------------------------------------------------------------- */

static void on_alert(const rules_alert_t *alert, void *ctx){
    (void) ctx;
    ESP_LOGW(TAG, "alert %s %s [%s]", rules_alert_type_name(alert->type),
             rules_alert_state_name(alert->state), alert->id);

    publish_alert(alert);

    if (alert->type == ALERT_FALL || alert->type == ALERT_HELP) {
        if (alert->state == ALERT_ACTIVE) {
            indicators_buzzer_start(FALL_BUZZER_MS);
        } else if (!rules_resident_alert_active(&s_rules)) {
            /* Only when nothing is open: cancelling a fall must not silence a help request still open. */
            indicators_buzzer_stop();
        }
    }
    if (alert->type == ALERT_WEARABLE_OFFLINE && alert->state == ALERT_ACTIVE) {
        wearable_cache_t *w = wearable_cache(alert->wearable);
        if (w != NULL) {
            publish_wearable(w, false);
        }
    }
}

/* CAN is a mirror of the inputs for the bus; a frame that cannot go is only logged. */
static void can_send(uint8_t sensor, const uint8_t *data, size_t len){
    esp_err_t err = can_transmit(sensor, data, len);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "CAN 0x%X not sent: %s", (unsigned) sensor, esp_err_to_name(err));
    }
}

/* Which side of the temperature rule's range a reading is on: -1 below, 0 inside, +1 above. */
static int temp_side(float celsius){
    if (celsius < s_params.temp_min_c) {
        return -1;
    }
    return celsius > s_params.temp_max_c ? 1 : 0;
}

static void handle_reading(const sensor_reading_t *reading, const rules_clock_t *now){
    switch (reading->kind) {
        case SENSOR_KIND_PRESENCE: {
            ESP_LOGI(TAG, "presence %s", reading->active ? "detected" : "clear");
            s_state.presence_known = true;
            s_state.presence = reading->active;
            uint8_t value = reading->active;
            can_send(CAN_NIBBLE_PRESENCE, &value, CAN_LEN_PRESENCE);
            publish_presence(reading->active);
            rules_on_presence(&s_rules, reading->active, now);
            break;
        }
        case SENSOR_KIND_DOOR: {
            size_t door = reading->index;
            if (door >= s_door_count) {
                break;
            }
            ESP_LOGI(TAG, "door %s %s", s_door_ids[door], reading->active ? "open" : "closed");
            s_state.door_known[door] = true;
            s_state.door_open[door] = reading->active;
            uint8_t value = reading->active;
            can_send(CAN_NIBBLE_DOOR(door), &value, CAN_LEN_DOOR);
            publish_door(door, reading->active);
            rules_on_door(&s_rules, door, reading->active, now);
            break;
        }
        case SENSOR_KIND_TEMPERATURE: {
            rules_on_temperature(&s_rules, reading->celsius, now);
            /*
             * Out only when it changed, or as a sign of life (docs/protocolo.md §1); and at once when
             * it crosses a limit of the temperature rule, so the series shows what an alert is about.
             */
            bool crossed = s_state.temp_known && temp_side(reading->celsius) != temp_side(s_state.temp_c);
            if (!crossed && !report_due(&s_state.temp_report, reading->celsius, MQTT_TEMP_REPORT_DELTA_C,
                                        TEMP_REPORT_MAX_MS, now->mono_ms)) {
                break;
            }
            report_mark(&s_state.temp_report, reading->celsius, now->mono_ms);
            s_state.temp_known = true;
            s_state.temp_c = reading->celsius;
            /* CAN: int16 little endian, 1/100 degC (docs/protocolo.md §2). */
            uint8_t payload[CAN_LEN_TEMPERATURE];
            can_put_i16_le(payload, (int16_t) lroundf(reading->celsius * 100.0f));
            can_send(CAN_NIBBLE_TEMPERATURE, payload, sizeof(payload));
            publish_temperature(reading->celsius);
            break;
        }
    }
}

static void handle_espnow(const espnow_rx_msg_t *msg, const rules_clock_t *now){
    wearable_cache_t *w = wearable_cache(msg->wearable);
    if (w == NULL) {
        ESP_LOGW(TAG, "too many wearables, %s ignored", msg->wearable);
        return;
    }
    bool was_offline = rules_wearable_offline(&s_rules, msg->wearable);
    w->rssi = msg->rssi;
    rules_on_wearable_seen(&s_rules, msg->wearable, now);
    if (was_offline) {
        /*
         * Back: the next heartbeat is published at once, without the activity from before the
         * silence. Until it arrives, the battery and activity cached here are old: "online"
         * goes out without them (a heartbeat sets this again).
         */
        report_reset(&w->report);
        (void) report_rms_take(&w->activity);
        w->heard_heartbeat = false;
    }

    switch (msg->type) {
        case ESPNOW_MSG_HEARTBEAT:
            w->heard_heartbeat = true;
            w->battery_mv = msg->heartbeat.battery_mv;
            report_rms_add(&w->activity, msg->heartbeat.activity_mg);
            if (msg->heartbeat.battery_mv != ESPNOW_BATTERY_UNKNOWN) {
                rules_on_battery(&s_rules, msg->wearable, msg->heartbeat.battery_mv, now);
            }
            /*
             * Every heartbeat keeps the wearable alive for the rules (offline after 90 s), but
             * pulseira/<id> gets the first one and then a summary every few minutes
             * (docs/protocolo.md §1): the database stores what changed, not each 30 s.
             */
            if (report_due(&w->report, 0.0f, 0.0f, WEARABLE_REPORT_MAX_MS, now->mono_ms)) {
                w->activity_mg = report_rms_take(&w->activity);
                publish_wearable(w, true);
                report_mark(&w->report, 0.0f, now->mono_ms);
            }
            return;
        case ESPNOW_MSG_FALL:
            ESP_LOGW(TAG, "FALL from %s, a2 = %.2f g2, P = %u %%", msg->wearable, msg->fall.a2_peak,
                     msg->fall.prob_pct);
            if (!isfinite(msg->fall.a2_peak) || msg->fall.a2_peak < 0.0f || msg->fall.prob_pct > 100) {
                ESP_LOGW(TAG, "FALL from %s with values out of range: the alert goes without them", msg->wearable);
            }
            rules_on_fall(&s_rules, msg->wearable, msg->seq, msg->uptime_ms, msg->fall.a2_peak,
                          msg->fall.prob_pct / 100.0f, now);
            break;
        case ESPNOW_MSG_FALL_CANCEL:
            ESP_LOGI(TAG, "FALL_CANCEL from %s for seq %u", msg->wearable, msg->fall_cancel.fall_seq);
            rules_on_fall_cancel(&s_rules, msg->wearable, msg->fall_cancel.fall_seq, now);
            break;
        case ESPNOW_MSG_HELP:
            ESP_LOGW(TAG, "HELP from %s (button held), seq %u", msg->wearable, msg->seq);
            rules_on_help(&s_rules, msg->wearable, msg->seq, msg->uptime_ms, now);
            break;
        case ESPNOW_MSG_HELP_CANCEL:
            ESP_LOGI(TAG, "HELP_CANCEL from %s for seq %u", msg->wearable, msg->help_cancel.help_seq);
            rules_on_help_cancel(&s_rules, msg->wearable, msg->help_cancel.help_seq, now);
            break;
        default:
            break;
    }
    if (was_offline) {
        publish_wearable(w, true);
    }
}

static void handle_config(char *json, size_t len){
    rules_params_t next;
    char err[96];

    if (!hub_config_from_json(json, len, &s_params, s_door_ids, s_door_count, &next, err, sizeof(err))) {
        ESP_LOGW(TAG, "configuration refused: %s", err);
        publish_config_applied(err);
        free(json);
        return;
    }

    bool changed = memcmp(&next, &s_params, sizeof(next)) != 0;
    s_params = next;
    s_config_origin = HUB_CONFIG_MQTT;
    rules_set_params(&s_rules, &s_params);

    /* The retained configuration comes back on every reconnection: only write flash on a change. */
    if (changed) {
        char *full = hub_config_to_json(&s_params, s_door_ids, s_door_count, s_config_origin, NULL);
        esp_err_t save_err = full ? hub_config_save(full) : ESP_ERR_NO_MEM;
        if (save_err != ESP_OK) {
            ESP_LOGW(TAG, "configuration applied but not saved: %s", esp_err_to_name(save_err));
        }
        free(full);
        ESP_LOGI(TAG, "configuration applied");
    }
    publish_config_applied(NULL);
    free(json);
}

/* Everything retained is republished, so a broker that lost its state catches up. */
static void handle_mqtt_up(void){
    publish_status();
    publish_config_applied(NULL);
    if (s_state.presence_known) {
        publish_presence(s_state.presence);
    }
    for (size_t i = 0; i < s_door_count; i++) {
        if (s_state.door_known[i]) {
            publish_door(i, s_state.door_open[i]);
        }
    }
    if (s_state.temp_known) {
        publish_temperature(s_state.temp_c);
    }
    for (size_t i = 0; i < RULES_MAX_WEARABLES; i++) {
        if (s_wearables[i].used) {
            publish_wearable(&s_wearables[i], !rules_wearable_offline(&s_rules, s_wearables[i].id));
        }
    }
}

static void update_status_led(void){
    if (rules_any_active(&s_rules)) {
        indicators_set_status(IND_STATUS_ALERT);
    } else if (!mqtt_link_connected()) {
        indicators_set_status(IND_STATUS_NO_NETWORK);
    } else {
        indicators_set_status(IND_STATUS_OK);
    }
}

static void hub_task(void *arg){
    (void) arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
    rules_clock_t now;
    hub_event_t ev;

    while (1) {
        if (xQueueReceive(s_queue, &ev, pdMS_TO_TICKS(HUB_TICK_MS)) == pdTRUE) {
            hub_clock(&now);
            switch (ev.kind) {
                case HUB_EV_SENSOR:  handle_reading(&ev.reading, &now);           break;
                case HUB_EV_ESPNOW:  handle_espnow(&ev.espnow, &now);             break;
                case HUB_EV_CONFIG:  handle_config(ev.config.json, ev.config.len); break;
                case HUB_EV_NET:
                    ESP_LOGI(TAG, "network %s", ev.net_up ? "up" : "down");
                    break;
            }
        }

        if (atomic_exchange(&s_mqtt_up_pending, false)) {
            handle_mqtt_up();
        }
        hub_clock(&now);
        rules_tick(&s_rules, &now);
        if (now.time_valid != s_reported_time_valid) {
            s_reported_time_valid = now.time_valid;
            publish_status();   /* "hora" changed: the night rules just came alive */
        }
        update_status_led();
        esp_task_wdt_reset();
    }
}

/* ---------------------------------------------------------------------------
 * Producers: they only queue (any task)
 * ------------------------------------------------------------------------- */

static void post(const hub_event_t *ev){
    if (xQueueSend(s_queue, ev, 0) != pdTRUE) {
        ESP_LOGW(TAG, "hub queue full, event %d dropped", ev->kind);
        if (ev->kind == HUB_EV_CONFIG) {
            free(ev->config.json);
        }
    }
}

static void on_sensor(const sensor_reading_t *reading, void *ctx){
    (void) ctx;
    hub_event_t ev = { .kind = HUB_EV_SENSOR, .reading = *reading };
    post(&ev);
}

static void on_espnow(const espnow_rx_msg_t *msg, void *ctx){
    (void) ctx;
    hub_event_t ev = { .kind = HUB_EV_ESPNOW, .espnow = *msg };
    post(&ev);
}

/*
 * Runs in the MQTT task. A flag, not a queued event: with the queue full the event was lost,
 * and the retained "offline" from the Last Will stayed on the broker. hub_task sees the flag
 * on its next pass (at most HUB_TICK_MS later).
 */
static void on_mqtt_connected(void *ctx){
    (void) ctx;
    atomic_store(&s_mqtt_up_pending, true);
}

static void on_mqtt_config(const char *json, size_t len, void *ctx){
    (void) ctx;
    char *copy = malloc(len + 1);
    if (copy == NULL) {
        ESP_LOGE(TAG, "out of memory for the configuration");
        return;
    }
    memcpy(copy, json, len);
    copy[len] = '\0';
    hub_event_t ev = { .kind = HUB_EV_CONFIG, .config = { .json = copy, .len = len } };
    post(&ev);
}

static void on_net(bool connected, void *ctx){
    (void) ctx;
    hub_event_t ev = { .kind = HUB_EV_NET, .net_up = connected };
    post(&ev);
}

/* Any door or window opening on the bus, whoever reports it. Runs in can_rx. */
static void on_contact(const can_rx_event_t *event, const can_id_info_t *info, void *ctx){
    (void) ctx;

    if (event->length < 1) {
        return;
    }
    ESP_LOGI(TAG, "CAN %s/%s is %s",
             info->group->name, info->sensor->name,
             event->data[0] ? "OPEN" : "CLOSED");
}

/*
 * The wearable's frames, on the bench only (docs/protocolo.md §2): logged, never fed
 * to the rules. In use the wearable reaches the hub over ESP-NOW, which carries the
 * sequence numbers the fall alert depends on. Runs in can_rx.
 */
static void on_wearable_can(const can_rx_event_t *event, const can_id_info_t *info, void *ctx){
    (void) ctx;

    if (info->sensor->kind == CAN_SENSOR_KIND_FALL && event->length >= CAN_LEN_FALL) {
        ESP_LOGW(TAG, "CAN fall from the wearable, a2 = %.2f g2, P = %u %%", can_get_i16_le(event->data) / 100.0f,
                 event->data[2]);
    } else if (info->sensor->kind == CAN_SENSOR_KIND_WEARABLE && event->length >= CAN_LEN_WEARABLE) {
        ESP_LOGI(TAG, "CAN wearable heartbeat: battery %u mV, activity %u mg",
                 can_get_u16_le(event->data), can_get_u16_le(event->data + 2));
    }
}

/* ---------------------------------------------------------------------------
 * Boot
 * ------------------------------------------------------------------------- */

/* Logs the bus directory once, so a boot log shows which ids we can resolve. */
static void log_bus_map(void){
    size_t group_count = 0;
    const can_group_desc_t *groups = can_ids_groups(&group_count);

    for (size_t g = 0; g < group_count; g++) {
        for (uint8_t s = 0; s < groups[g].sensor_count; s++) {
            const can_sensor_desc_t *sensor = &groups[g].sensors[s];
            ESP_LOGI(TAG, "0x%03X  %s/%s",
                     (unsigned) CAN_MSG_ID(groups[g].group, sensor->nibble),
                     groups[g].name, sensor->name);
        }
    }
}

/* Built-in values, the door roles of the board, then whatever was saved last. */
static void load_config(void){
    rules_default_params(&s_params);
#define DOOR_DEFAULT_ROLE(index, gpio, id, role)   s_params.door_role[index] = DOOR_ROLE_##role;
    BOARD_DOORS(DOOR_DEFAULT_ROLE)
#undef DOOR_DEFAULT_ROLE

    char *json = NULL;
    if (hub_config_load(&json) != ESP_OK || json == NULL) {
        ESP_LOGI(TAG, "no saved configuration, using defaults");
        return;
    }
    rules_params_t saved;
    char err[96];
    if (hub_config_from_json(json, strlen(json), &s_params, s_door_ids, s_door_count, &saved, err, sizeof(err))) {
        s_params = saved;
        s_config_origin = HUB_CONFIG_NVS;
        ESP_LOGI(TAG, "configuration restored from NVS");
    } else {
        ESP_LOGW(TAG, "saved configuration refused (%s), using defaults", err);
    }
    free(json);
}

static void init_nvs(void){
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

void app_main(void){
    ESP_ERROR_CHECK(sys_events_init());
    init_nvs();

    indicators_config_t indicators = {
        .led_gpio = PIN_STATUS_LED,
        .buzzer_gpio = PIN_BUZZER,
    };
    ESP_ERROR_CHECK(indicators_init(&indicators));
    indicators_set_status(IND_STATUS_BOOT);

    s_queue = xQueueCreate(HUB_QUEUE_DEPTH, sizeof(hub_event_t));
    ESP_ERROR_CHECK(s_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    s_door_ids = reedswitch_ids(&s_door_count);
    load_config();
    rules_clock_t now;
    hub_clock(&now);
    rules_init(&s_rules, &s_params, s_door_ids, s_door_count, on_alert, NULL, &now);
#ifndef CONFIG_HUB_PIR_FITTED
    rules_set_presence_fitted(&s_rules, false);
#endif

    log_bus_map();

    // ========== SENSORS ==========
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(sensor_events_init(on_sensor, NULL));
    /*
     * A sensor that is not fitted (menu "Room hub: sensors") is skipped with one warning:
     * an unplugged PIR or reed switch would read as "nobody" or "open" and raise false
     * alerts, and nothing in the signal tells them apart from a real one.
     */
#ifdef CONFIG_HUB_PIR_FITTED
    ESP_ERROR_CHECK(hcsr501_init());
#else
    ESP_LOGW(TAG, "presence sensor not fitted (menuconfig): no presence; inactivity and night exit rules "
             "suspended");
#endif
    ESP_ERROR_CHECK(reedswitch_init());
    esp_err_t err = ESP_OK;
#ifdef CONFIG_HUB_LM35_FITTED
    err = lm35_init();
    if (err != ESP_OK) {
        /* One missing sensor must not take the doors and the fall alarm down with it. */
        ESP_LOGW(TAG, "temperature off: %s", esp_err_to_name(err));
    }
#else
    ESP_LOGW(TAG, "temperature sensor not fitted (menuconfig): no temperature, no temperature rule");
#endif

    // ========== CAN ==========
    /* Handlers must be registered before can_init(), which starts can_rx. */
    ESP_ERROR_CHECK(can_rx_handler_register(CAN_GROUP_ANY, CAN_SENSOR_KIND_CONTACT, on_contact, NULL));
    ESP_ERROR_CHECK(can_rx_handler_register(CAN_GROUP_1, CAN_SENSOR_KIND_FALL, on_wearable_can, NULL));
    ESP_ERROR_CHECK(can_rx_handler_register(CAN_GROUP_1, CAN_SENSOR_KIND_WEARABLE, on_wearable_can, NULL));
    can_config_t can_config = {
        .tx_gpio = PIN_CAN_TX,
        .rx_gpio = PIN_CAN_RX,
        .group = CAN_GROUP_PROJECT,
    };
    err = can_init(&can_config);
    if (err != ESP_OK) {
        /* The rest of the hub never depends on the bus. A missing bus is reported by can itself. */
        ESP_LOGW(TAG, "CAN off: %s", esp_err_to_name(err));
    }

    // ========== NETWORK ==========
    /* Mains powered: the radio never dozes, or ESP-NOW frames arriving between beacons are lost. */
    net_config_t net_config = {
        .join_ap = true,
        .power_save = false,
        .on_change = on_net,
    };
    ESP_ERROR_CHECK(net_init(&net_config));
    err = espnow_link_init(on_espnow, NULL);
    if (err != ESP_OK) {
        /* Doors, presence and temperature carry on; the wearable it expects will be reported offline. */
        ESP_LOGE(TAG, "ESP-NOW off (%s): the wearable is not heard", esp_err_to_name(err));
    }
    char wearable[ESPNOW_WEARABLE_ID_LEN];
    if (espnow_link_paired_wearable(wearable)) {
        /* Expected from boot: a wearable that never shows up is reported offline. */
        rules_expect_wearable(&s_rules, wearable, &now);
        wearable_cache(wearable);
    }
    mqtt_link_config_t mqtt_config = {
        .will_suffix = MQTT_TOPIC_STATUS,
        /* No timestamp: the broker sends it long after the hub is gone. */
        .will_payload = "{\"v\":1,\"estado\":\"" MQTT_STATE_OFFLINE "\"}",
        .subscribe_suffix = MQTT_TOPIC_CONFIG_WANTED,
        .on_connected = on_mqtt_connected,
        .on_message = on_mqtt_config,
    };
    err = mqtt_link_start(&mqtt_config);
    if (err != ESP_OK) {
        /* mqtt_link said why; the rules, the LED and CAN still work without the central. */
        ESP_LOGE(TAG, "MQTT off: %s", esp_err_to_name(err));
    }

    /* Core 0, with the radio and the protocols it feeds: core 1 stays with the inputs. */
    ESP_ERROR_CHECK(esp_err_from_pdpass(xTaskCreatePinnedToCore(hub_task, "hub_task", STACK_LARGE, NULL,
                                                                PRIO_MEDIA, NULL, CORE_PROTOCOL)));
}
