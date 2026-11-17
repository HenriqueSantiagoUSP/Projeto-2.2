#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gpio_filter.h"
#include "driver/i2c_master.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "battery.h"
#include "board_pins.h"
#include "can.h"
#include "espnow_tx.h"
#include "fall_pipeline.h"
#include "indicators.h"
#include "mpu6050.h"
#include "mqtt_link.h"
#include "net.h"
#include "protocol_can.h"
#include "protocol_espnow.h"
#include "protocol_mqtt.h"
#include "rtos_err.h"
#include "signal_proc.h"
#include "sys_events.h"
#include "task_config.h"

static const char *TAG = "main";

#define SAMPLE_BATCH_MS     50      /* FIFO drained every 50 ms: 10 samples, far from its 85 */
#define FIFO_BATCH_MAX      64
#define BUTTON_DEBOUNCE_MS  300
#define COMM_QUEUE_DEPTH    8
#define COMM_MAX_WAIT_MS    500     /* comm_task wakes this often to feed the watchdog */
#define LOCK_WAIT_MS        100     /* no task waits forever for the activity lock */
#define NET_WAIT_MS         10000   /* with MQTT on: how long the first contact waits for the AP */
#define IMU_RETRY_MS        5000    /* without the MPU-6050, look for it again this often */
#define IMU_LOST_AFTER      10      /* consecutive failed FIFO reads (0.5 s) before the sensor counts as gone */
/* A wrong length is a key or MAC the two boards would read differently: refuse to build. */
#ifdef CONFIG_GADGET_ESPNOW_ENCRYPT
_Static_assert(sizeof(CONFIG_GADGET_ESPNOW_PMK) - 1 == 16,
               "menuconfig > Wearable > the PMK must have exactly 16 characters");
_Static_assert(sizeof(CONFIG_GADGET_ESPNOW_LMK) - 1 == 16,
               "menuconfig > Wearable > the LMK must have exactly 16 characters");
#endif
_Static_assert(sizeof(CONFIG_GADGET_HUB_MAC) == 1 || sizeof(CONFIG_GADGET_HUB_MAC) == 18,
               "menuconfig > Wearable > the hub MAC must be empty or aa:bb:cc:dd:ee:ff");

#define RETRY_FIRST_MS      2000    /* an undelivered FALL / FALL_CANCEL is tried again after this... */
#define RETRY_MAX_MS        30000   /* ...doubling up to this, until it gets through */
#define RETRY_SCAN_EVERY    3       /* every 3rd retry looks for the hub on every channel first */

#define WEARABLE_ID_LEN     7       /* last 3 MAC bytes in hex, as the hub names this wearable */
#define JSON_MAX            256

/*
 * Everything that reaches comm_task is a comm_event_t, copied by value into its
 * queue: the sampling task, the button ISR and the MQTT task never touch the
 * radio, the CAN node or comm_task's state.
 */
typedef enum {
    COMM_EV_FALL,       /* sampling_task: the classifier said fall (outside the cooldown) */
    COMM_EV_WINDOW,     /* sampling_task: every classified window, for the log */
    COMM_EV_BUTTON,     /* button ISR */
} comm_event_kind_t;

typedef struct {
    float   a2_peak;    /* filtered |a|^2 at the trigger peak, g^2; 0 = simulated */
    uint8_t prob_pct;   /* the classifier's P(fall), % */
} fall_info_t;

typedef struct {
    comm_event_kind_t kind;
    union {
        fall_info_t fall;                                                   /* COMM_EV_FALL */
        struct { fp_result_t r; uint32_t cycles; bool reported; } window;   /* COMM_EV_WINDOW */
    };
} comm_event_t;

/* State of comm_task; nobody else reads it. */
typedef struct {
    bool     link_ok;            /* the hub ACKed the last heartbeat */
    bool     fall_pending;       /* inside the cancel window */
    uint16_t fall_seq;
    bool     fall_via_mqtt;      /* the hub never ACKed it: the alert went out over MQTT */
    int64_t  cancel_deadline_ms;
    int64_t  last_button_ms;
    int64_t  next_heartbeat_ms;

    /*
     * A FALL (or FALL_CANCEL) that reached nobody is kept and resent unchanged (same seq and
     * uptime, so the hub sees one fall) until it is delivered: at the backoff below, and at
     * once when a heartbeat gets through again.
     */
    bool                 fall_undelivered;
    espnow_fall_t        fall_frame;
    fall_info_t          fall_info;
    bool                 cancel_undelivered;
    espnow_fall_cancel_t cancel_frame;
    int64_t              retry_at_ms;
    int64_t              retry_delay_ms;
    unsigned             retries;
} comm_state_t;

static QueueHandle_t s_comm_queue = NULL;
static mpu6050_t s_mpu;                           /* sampling_task only, after boot */
static i2c_master_bus_handle_t s_i2c_bus = NULL;  /* written once at boot */
static fall_pipeline_t s_pipeline;                /* sampling_task only (~20 KB, not on its stack) */
static fp_classifier_t s_classifier;              /* written once at boot */

/*
 * GADGET_HUB_MAC empty: no hub to talk to (a bench or a demo with one board). ESP-NOW
 * is never started, and falls go to the log (and to MQTT and CAN when they are on).
 */
static bool s_has_hub = false;                    /* written at boot, before the tasks start */
/* Set where a queued event could be lost (MQTT task, button ISR), taken by comm_task. */
static atomic_bool s_mqtt_up_pending = false;
static atomic_bool s_button_missed = false;

/* Activity between heartbeats: sampling_task merges each batch, comm_task takes the average. */
static sp_activity_t s_activity;
static SemaphoreHandle_t s_activity_lock = NULL;  /* mutex: priority inheritance */

static char s_wearable_id[WEARABLE_ID_LEN];       /* written once at boot */

static int64_t now_ms(void){
    return esp_timer_get_time() / 1000;
}

/* ---------------------------------------------------------------------------
 * Sampling (core 1): the timing comes from the MPU-6050, this task only drains it
 * ------------------------------------------------------------------------- */

static void post_from_sampling(const comm_event_t *ev){
    /* Never block the sampling: a full queue costs this event, not the next samples. */
    if (xQueueSend(s_comm_queue, ev, 0) != pdTRUE) {
        ESP_LOGE(TAG, "comm queue full: event %d lost", ev->kind);
    }
}

static uint8_t prob_to_pct(float prob){
    float pct = roundf(prob * 100.0f);
    return (uint8_t) (pct < 0.0f ? 0.0f : (pct > 100.0f ? 100.0f : pct));
}

/*
 * A window just closed: features, classifier, and the decision. After a reported
 * fall, the same fall still shaking (or a second one) is not reported again for
 * CONFIG_GADGET_FALL_COOLDOWN_S; the window still goes to the log.
 */
static void classify_window(void){
    static bool s_fell = false;
    static uint32_t s_last_fall_index = 0;
    const uint32_t cooldown = (uint32_t) CONFIG_GADGET_FALL_COOLDOWN_S * MPU6050_SAMPLE_RATE_HZ;

    comm_event_t ev = { .kind = COMM_EV_WINDOW };
    ev.window.cycles = fall_pipeline_classify_timed(&s_pipeline, &ev.window.r);
    const fp_result_t *r = &ev.window.r;
    /* Unsigned subtraction: correct across the wrap of the sample count. */
    bool cooling = s_fell && r->peak_index - s_last_fall_index < cooldown;
    ev.window.reported = r->fall && !cooling;

    if (ev.window.reported) {
        s_fell = true;
        s_last_fall_index = r->peak_index;
        comm_event_t fall = {
            .kind = COMM_EV_FALL,
            .fall = { .a2_peak = r->a_peak * r->a_peak, .prob_pct = prob_to_pct(r->prob) },
        };
        post_from_sampling(&fall);   /* first: if the queue is short, the alert matters most */
    }
    post_from_sampling(&ev);
}

/*
 * Brings the MPU-6050 up and restarts the pipeline from a clean state, so a window
 * never mixes samples from before and after a gap. sampling_task only.
 */
static void pipeline_restart(void){
    fall_pipeline_init(&s_pipeline, CONFIG_GADGET_FALL_TRIGGER_CG / 100.0f, s_classifier);
#ifdef CONFIG_GADGET_COLLECT_MODE
    /* A header starts every run of samples: the sample count (t_ms, pico) restarts here. */
    printf("CSV:t_ms,ax,ay,az,gx,gy,gz  (raw counts; %.0f LSB/g, %.1f LSB/dps, %d Hz)\n",
           MPU6050_ACCEL_LSB_PER_G, MPU6050_GYRO_LSB_PER_DPS, MPU6050_SAMPLE_RATE_HZ);
#endif
}

static bool imu_start(void){
    if (mpu6050_init(&s_mpu, s_i2c_bus, MPU6050_ADDR_DEFAULT) != ESP_OK) {
        return false;
    }
    pipeline_restart();
    sys_events_set(SYS_EV_IMU_OK);
    return true;
}

static void sampling_task(void *arg){
    (void) arg;
    static mpu6050_sample_t batch[FIFO_BATCH_MAX];

    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
#ifdef CONFIG_GADGET_COLLECT_MODE
    uint32_t sample_index = 0;   /* the sensor's clock: one tick per sample */
#endif
    /*
     * Without the sensor the wearable keeps running, with the LED in fault and no
     * heartbeats (comm_task), so the hub still reports it offline. It looks for the
     * sensor again every IMU_RETRY_MS, and says so once, not on every try.
     */
    bool imu_ok = imu_start();
    int failures = 0;
    int empty_reads = 0;
    bool samples_lost = false;
    int64_t next_try_ms = 0;
    if (!imu_ok) {
        ESP_LOGW(TAG, "MPU-6050 not connected (I2C SDA GPIO%d, SCL GPIO%d): no fall detection, looking for it "
                 "every %d s", PIN_I2C_SDA, PIN_I2C_SCL, IMU_RETRY_MS / 1000);
        next_try_ms = now_ms() + IMU_RETRY_MS;
    }
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SAMPLE_BATCH_MS));
        esp_task_wdt_reset();

        if (!imu_ok) {
            if (now_ms() < next_try_ms) {
                continue;
            }
            imu_ok = imu_start();
            if (!imu_ok) {
                next_try_ms = now_ms() + IMU_RETRY_MS;
                continue;
            }
            ESP_LOGI(TAG, "MPU-6050 connected: fall detection on");
            failures = 0;
            empty_reads = 0;
            samples_lost = false;   /* imu_start already restarted the pipeline */
#ifdef CONFIG_GADGET_COLLECT_MODE
            sample_index = 0;
#endif
            last_wake = xTaskGetTickCount();
            continue;
        }

        size_t n = 0;
        bool overflow = false;
        esp_err_t err = mpu6050_fifo_read(&s_mpu, batch, FIFO_BATCH_MAX, &n, &overflow);
        if (err == ESP_OK && overflow) {
            ESP_LOGW(TAG, "FIFO overflow: samples lost");
            samples_lost = true;
            failures = 0;
            empty_reads = 0;
            continue;
        }
        /*
         * A sensor that reset itself (a dip in its supply) comes back asleep with the FIFO off:
         * every read then succeeds with no samples. A run of empty reads is a lost sensor too,
         * and the retry below configures it again. At 200 Hz each read should bring ~10.
         */
        if (err == ESP_OK && n == 0 && ++empty_reads >= IMU_LOST_AFTER) {
            err = ESP_ERR_INVALID_RESPONSE;
        }
        if (err != ESP_OK) {
            samples_lost = true;
            /* One bad read is noise on the bus; a run of them is a sensor that came loose. */
            if (err == ESP_ERR_INVALID_RESPONSE || ++failures >= IMU_LOST_AFTER) {
                imu_ok = false;
                sys_events_clear(SYS_EV_IMU_OK);
                ESP_LOGW(TAG, "MPU-6050 stopped %s (%s): no fall detection, looking for it every %d s",
                         err == ESP_ERR_INVALID_RESPONSE ? "sending samples" : "answering", esp_err_to_name(err),
                         IMU_RETRY_MS / 1000);
                next_try_ms = now_ms() + IMU_RETRY_MS;
            }
            continue;
        }
        failures = 0;
        if (n > 0) {
            empty_reads = 0;
        }
        if (samples_lost && n > 0) {
            /* A window must not join samples from both sides of a gap. */
            pipeline_restart();
#ifdef CONFIG_GADGET_COLLECT_MODE
            sample_index = 0;
#endif
            samples_lost = false;
        }

        sp_activity_t batch_activity = { 0 };
        for (size_t i = 0; i < n; i++) {
            const mpu6050_sample_t *s = &batch[i];
#ifdef CONFIG_GADGET_COLLECT_MODE
            uint32_t t_ms = sample_index++ * MPU6050_SAMPLE_PERIOD_MS;
            printf("CSV:%lu,%d,%d,%d,%d,%d,%d\n", (unsigned long) t_ms, s->ax, s->ay, s->az, s->gx, s->gy, s->gz);
#endif
            const float sample[FP_CHANNELS] = {
                s->ax / MPU6050_ACCEL_LSB_PER_G, s->ay / MPU6050_ACCEL_LSB_PER_G, s->az / MPU6050_ACCEL_LSB_PER_G,
                s->gx / MPU6050_GYRO_LSB_PER_DPS, s->gy / MPU6050_GYRO_LSB_PER_DPS, s->gz / MPU6050_GYRO_LSB_PER_DPS,
            };
            /* Activity on the raw |a|, as before; the pipeline filters its own copy. */
            sp_activity_add(&batch_activity, sqrtf(sample[0] * sample[0] + sample[1] * sample[1] +
                                                   sample[2] * sample[2]));

            if (fall_pipeline_feed(&s_pipeline, sample)) {
                classify_window();
            }
        }

        /* The lock covers only the merge, never the I2C read or the printing above. */
        if (xSemaphoreTake(s_activity_lock, pdMS_TO_TICKS(LOCK_WAIT_MS)) == pdTRUE) {
            sp_activity_merge(&s_activity, &batch_activity);
            xSemaphoreGive(s_activity_lock);
        }
    }
}

/* ---------------------------------------------------------------------------
 * MQTT (optional): telemetry, and the fall alert when the hub does not answer
 * ------------------------------------------------------------------------- */

#ifdef CONFIG_GADGET_MQTT

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

/* "pulseira/<id>/<suffix>" */
static void wearable_suffix(char *out, size_t out_len, const char *suffix){
    snprintf(out, out_len, "%s/%s/%s", MQTT_TOPIC_WEARABLE, s_wearable_id, suffix);
}

static void publish_link_online(void){
    char suffix[48], json[JSON_MAX];
    wearable_suffix(suffix, sizeof(suffix), MQTT_TOPIC_WEARABLE_LINK);
    snprintf(json, sizeof(json), "{\"v\":%d,\"estado\":\"%s\",\"fw\":\"%s\"}",
             MQTT_JSON_VERSION, MQTT_STATE_ONLINE, esp_app_get_description()->version);
    mqtt_link_publish(suffix, json, 1, true);
}

static void publish_telemetry(uint16_t battery_mv, uint16_t activity_mg, bool hub_ok){
    char suffix[48], ts[32], battery[32] = "", json[JSON_MAX];
    wearable_suffix(suffix, sizeof(suffix), MQTT_TOPIC_WEARABLE_TELEMETRY);
    ts_field(ts, sizeof(ts));
    if (battery_mv != ESPNOW_BATTERY_UNKNOWN) {
        snprintf(battery, sizeof(battery), ",\"bateria_mV\":%u", battery_mv);
    }
    snprintf(json, sizeof(json), "{\"v\":%d%s,\"atividade\":%.3f,\"canal\":%u,\"hub\":%s%s}",
             MQTT_JSON_VERSION, battery, activity_mg / 1000.0f, espnow_tx_channel(),
             hub_ok ? "true" : "false", ts);
    /* QoS 0, not retained: the next one comes with the next heartbeat. */
    mqtt_link_publish(suffix, json, 0, false);
}

/*
 * The same alert the hub would publish for this fall (docs/protocolo.md §1): same id
 * "<wearable>-<seq>", so if the hub got it after all the notifier drops the copy.
 */
static bool publish_fall_alert(uint16_t seq, const fall_info_t *fall, bool active){
    if (!mqtt_link_connected()) {
        return false;
    }
    char ts[32], detail[64], json[JSON_MAX];
    ts_field(ts, sizeof(ts));
    if (active) {
        snprintf(detail, sizeof(detail), "\"a2\":%.2f,\"prob\":%.2f,\"via\":\"pulseira\"", fall->a2_peak,
                 fall->prob_pct / 100.0f);
    } else {
        snprintf(detail, sizeof(detail), "\"via\":\"pulseira\"");
    }
    snprintf(json, sizeof(json),
             "{\"v\":%d,\"id\":\"%s-%u\",\"tipo\":\"%s\",\"estado\":\"%s\",\"origem\":\"%s/%s\",\"detalhe\":{%s}%s}",
             MQTT_JSON_VERSION, s_wearable_id, seq, MQTT_ALERT_FALL,
             active ? MQTT_ALERT_ACTIVE : MQTT_ALERT_CANCELLED, MQTT_TOPIC_WEARABLE, s_wearable_id,
             detail, ts);
    return mqtt_link_publish(MQTT_TOPIC_ALERT, json, 1, false) == ESP_OK;
}

/*
 * Runs in the MQTT task. A flag, not a queued event: with the queue full the event was lost
 * and the retained "offline" from the Last Will stayed. comm_task takes it on its next pass.
 */
static void on_mqtt_connected(void *ctx){
    (void) ctx;
    atomic_store(&s_mqtt_up_pending, true);
}

static esp_err_t start_mqtt(void){
    char will_suffix[48];
    wearable_suffix(will_suffix, sizeof(will_suffix), MQTT_TOPIC_WEARABLE_LINK);
    mqtt_link_config_t config = {
        .will_suffix = will_suffix,
        .will_payload = "{\"v\":1,\"estado\":\"" MQTT_STATE_OFFLINE "\"}",
        .on_connected = on_mqtt_connected,
    };
    return mqtt_link_start(&config);
}

#endif /* CONFIG_GADGET_MQTT */

/* ---------------------------------------------------------------------------
 * CAN (optional, bench and course requirement only)
 * ------------------------------------------------------------------------- */

static void can_send(uint8_t sensor, const uint8_t *data, size_t len){
#ifdef CONFIG_GADGET_CAN
    esp_err_t err = can_transmit(sensor, data, len);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "CAN 0x%X not sent: %s", (unsigned) sensor, esp_err_to_name(err));
    }
#else
    (void) sensor;
    (void) data;
    (void) len;
#endif
}

static void can_send_fall(const fall_info_t *fall){
    float centi = roundf(fall->a2_peak * 100.0f);
    uint8_t payload[CAN_LEN_FALL];
    can_put_i16_le(payload, (int16_t) (centi > INT16_MAX ? INT16_MAX : centi));
    payload[2] = fall->prob_pct;
    can_send(CAN_NIBBLE_FALL, payload, sizeof(payload));
}

static void can_send_heartbeat(uint16_t battery_mv, uint16_t activity_mg){
    uint8_t payload[CAN_LEN_WEARABLE];
    can_put_u16_le(payload, battery_mv);
    can_put_u16_le(payload + 2, activity_mg);
    can_send(CAN_NIBBLE_WEARABLE, payload, sizeof(payload));
}

/* ---------------------------------------------------------------------------
 * Communication (core 0)
 * ------------------------------------------------------------------------- */

static uint16_t take_activity_mg(void){
    uint16_t mg = 0;
    if (xSemaphoreTake(s_activity_lock, pdMS_TO_TICKS(LOCK_WAIT_MS)) == pdTRUE) {
        mg = sp_activity_take_mg(&s_activity);
        xSemaphoreGive(s_activity_lock);
    }
    return mg;
}

static bool send_heartbeat(void){
    espnow_heartbeat_t hb;
    espnow_tx_header(&hb.hdr, ESPNOW_MSG_HEARTBEAT);
    hb.battery_mv = battery_read_mv();
    hb.activity_mg = take_activity_mg();

    /* Never resent: the next one comes in 30 s, and the hub tolerates two lost. */
    bool delivered = s_has_hub && espnow_tx_send(&hb, sizeof(hb), 1) == ESP_OK;

    can_send_heartbeat(hb.battery_mv, hb.activity_mg);
#ifdef CONFIG_GADGET_MQTT
    publish_telemetry(hb.battery_mv, hb.activity_mg, delivered);
#endif
    return delivered;
}

/* Sends FALL until the hub ACKs, looking for it once if needed, then the fallbacks. */
static void send_fall(comm_state_t *st, const fall_info_t *info){
    espnow_fall_t fall;
    espnow_tx_header(&fall.hdr, ESPNOW_MSG_FALL);
    fall.a2_peak = info->a2_peak;
    fall.prob_pct = info->prob_pct;

    ESP_LOGW(TAG, "FALL (a2 = %.2f g2, P = %u %%), seq %u", info->a2_peak, info->prob_pct, fall.hdr.seq);
    can_send_fall(info);

    bool delivered = false;
    if (s_has_hub) {
        delivered = espnow_tx_send(&fall, sizeof(fall), ESPNOW_TX_RETRIES) == ESP_OK;
        if (!delivered) {
            espnow_tx_scan();
            delivered = espnow_tx_send(&fall, sizeof(fall), ESPNOW_TX_RETRIES) == ESP_OK;
        }
    }

    st->fall_seq = fall.hdr.seq;
    st->fall_via_mqtt = false;
    st->fall_undelivered = false;
    st->cancel_undelivered = false;   /* a new fall supersedes an older cancel still waiting */
    if (!delivered) {
#ifdef CONFIG_GADGET_MQTT
        st->fall_via_mqtt = publish_fall_alert(fall.hdr.seq, info, true);
#endif
        if (st->fall_via_mqtt) {
            ESP_LOGW(TAG, "%s: alert published over MQTT", s_has_hub ? "hub did not ACK the FALL" : "no hub");
        } else if (s_has_hub) {
            /* A fall that reached nobody: the one real error here. Kept and resent until it gets through. */
            ESP_LOGE(TAG, "FALL not delivered to the hub: trying again from %d s on", RETRY_FIRST_MS / 1000);
            st->fall_undelivered = true;
            st->fall_frame = fall;
            st->fall_info = *info;
            st->retry_delay_ms = RETRY_FIRST_MS;
            st->retry_at_ms = now_ms() + RETRY_FIRST_MS;
            st->retries = 0;
        }
        /* Stand-alone without MQTT: the FALL line above and the serial are where it goes. */
    }
}

static void send_fall_cancel(comm_state_t *st){
    espnow_fall_cancel_t cancel;
    espnow_tx_header(&cancel.hdr, ESPNOW_MSG_FALL_CANCEL);
    cancel.fall_seq = st->fall_seq;

    ESP_LOGI(TAG, "FALL_CANCEL for seq %u", st->fall_seq);
    /* The FALL may have reached the hub with its ACK lost: cancel there too, even if it is still queued here. */
    st->fall_undelivered = false;
    bool delivered = s_has_hub && espnow_tx_send(&cancel, sizeof(cancel), ESPNOW_TX_RETRIES) == ESP_OK;
    if (s_has_hub && !delivered) {
        ESP_LOGW(TAG, "FALL_CANCEL not delivered to the hub: trying again from %d s on", RETRY_FIRST_MS / 1000);
        st->cancel_undelivered = true;
        st->cancel_frame = cancel;
        st->retry_delay_ms = RETRY_FIRST_MS;
        st->retry_at_ms = now_ms() + RETRY_FIRST_MS;
        st->retries = 0;
    }
#ifdef CONFIG_GADGET_MQTT
    /* Cancel where the alert went: over MQTT if the hub never had it. */
    if (st->fall_via_mqtt || !delivered) {
        publish_fall_alert(st->fall_seq, NULL, false);
    }
#endif
}

/* One more try for a FALL or FALL_CANCEL that reached nobody; the frame goes out unchanged. */
static void retry_undelivered(comm_state_t *st){
    if (!st->fall_undelivered && !st->cancel_undelivered) {
        return;
    }
    st->retries++;
    if (st->retries % RETRY_SCAN_EVERY == 0) {
        espnow_tx_scan();
    }
    bool delivered;
    if (st->fall_undelivered) {
        delivered = espnow_tx_send(&st->fall_frame, sizeof(st->fall_frame), ESPNOW_TX_RETRIES) == ESP_OK;
#ifdef CONFIG_GADGET_MQTT
        if (!delivered && publish_fall_alert(st->fall_frame.hdr.seq, &st->fall_info, true)) {
            st->fall_via_mqtt = true;
            delivered = true;
        }
#endif
        if (delivered) {
            ESP_LOGW(TAG, "FALL seq %u delivered late, after %u retries", st->fall_frame.hdr.seq, st->retries);
            st->fall_undelivered = false;
        }
    } else {
        delivered = espnow_tx_send(&st->cancel_frame, sizeof(st->cancel_frame), ESPNOW_TX_RETRIES) == ESP_OK;
        if (delivered) {
            ESP_LOGI(TAG, "FALL_CANCEL for seq %u delivered late", st->cancel_frame.fall_seq);
            st->cancel_undelivered = false;
        }
    }
    if (!delivered) {
        ESP_LOGD(TAG, "still undelivered after %u retries", st->retries);
        st->retry_delay_ms = st->retry_delay_ms * 2 > RETRY_MAX_MS ? RETRY_MAX_MS : st->retry_delay_ms * 2;
        st->retry_at_ms = now_ms() + st->retry_delay_ms;
    }
}

static void log_window(const fp_result_t *r, uint32_t cycles, bool reported){
    uint32_t us = cycles / CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
    const char *verdict = reported ? "FALL" : (r->fall ? "fall (cooldown)" : "no fall");
    ESP_LOGI(TAG, "trigger at sample %lu: peak %.2f g, P(fall) %.2f -> %s (%lu cycles, ~%lu us)",
             (unsigned long) r->peak_index, r->a_peak, r->prob, verdict, (unsigned long) cycles,
             (unsigned long) us);
#ifdef CONFIG_GADGET_COLLECT_MODE
    /* One line per window, every feature by its Python name (modelo/queda/atributos.py). */
    printf("FEAT:pico=%lu,a_pico=%.4f", (unsigned long) r->peak_index, r->a_peak);
    for (int i = 0; i < FF_COUNT; i++) {
        printf(",%s=%.6g", fall_feature_names[i], r->features[i]);
    }
    printf(",prob=%.4f,queda=%d,cycles=%lu\n", r->prob, reported ? 1 : 0, (unsigned long) cycles);
#endif
}

static void handle_event(comm_state_t *st, comm_event_t *ev){
    int64_t now = now_ms();
    bool fall = false;
    fall_info_t info = { 0 };   /* a simulated fall: a2 = 0 and P = 0 mark it in the hub's logs */

    switch (ev->kind) {
        case COMM_EV_FALL:
            fall = true;
            info = ev->fall;
            break;

        case COMM_EV_WINDOW:
            log_window(&ev->window.r, ev->window.cycles, ev->window.reported);
            return;

        case COMM_EV_BUTTON:
            if (now - st->last_button_ms < BUTTON_DEBOUNCE_MS) {
                return;
            }
            st->last_button_ms = now;
            if (st->fall_pending && now < st->cancel_deadline_ms) {
                send_fall_cancel(st);
                st->fall_pending = false;
                indicators_buzzer_stop();
                return;
            }
#ifdef CONFIG_GADGET_BUTTON_SIMULATES_FALL
            ESP_LOGW(TAG, "button: simulated fall");
            fall = true;
#endif
            break;
    }

    if (fall) {
        send_fall(st, &info);
        st->fall_pending = true;
        st->cancel_deadline_ms = now + (int64_t) CONFIG_GADGET_CANCEL_WINDOW_S * 1000;
        indicators_buzzer_start((uint32_t) CONFIG_GADGET_CANCEL_WINDOW_S * 1000);
    }
}

static void comm_task(void *arg){
    (void) arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    if (s_has_hub && net_joins_ap()) {
        /* ESP-NOW rides on the access point's channel: wait for it (well inside the watchdog). */
        if (!sys_events_wait_all(SYS_EV_NET_CONNECTED, pdMS_TO_TICKS(NET_WAIT_MS))) {
            ESP_LOGD(TAG, "access point not joined yet; ESP-NOW tries anyway");
        }
        esp_task_wdt_reset();
    }

    comm_state_t st = {
        .link_ok = s_has_hub && espnow_tx_scan(),
        .next_heartbeat_ms = now_ms() + 2000,   /* announce ourselves soon after boot */
    };

    while (1) {
        comm_event_t ev;
        if (xQueueReceive(s_comm_queue, &ev, pdMS_TO_TICKS(COMM_MAX_WAIT_MS)) == pdTRUE) {
            handle_event(&st, &ev);
        }
        if (atomic_exchange(&s_button_missed, false)) {
            comm_event_t button = { .kind = COMM_EV_BUTTON };
            handle_event(&st, &button);
        }
        if (atomic_exchange(&s_mqtt_up_pending, false)) {
#ifdef CONFIG_GADGET_MQTT
            publish_link_online();
#endif
        }

        int64_t now = now_ms();
        if (st.fall_pending && now >= st.cancel_deadline_ms) {
            st.fall_pending = false;   /* the alarm now belongs to the hub and the caregivers */
        }
        bool imu_ok = sys_events_all(SYS_EV_IMU_OK);
        if (now >= st.next_heartbeat_ms) {
            /* No sensor, no heartbeat: a wearable that cannot detect falls must not look healthy. */
            if (imu_ok) {
                st.link_ok = send_heartbeat();
                if (st.link_ok) {
                    st.retry_at_ms = now;   /* the hub answers again: an undelivered FALL goes now */
                }
            }
            st.next_heartbeat_ms = now + ESPNOW_HEARTBEAT_PERIOD_MS;
        }
        if ((st.fall_undelivered || st.cancel_undelivered) && now >= st.retry_at_ms) {
            retry_undelivered(&st);
        }

        if (st.fall_pending) {
            indicators_set_status(IND_STATUS_ALERT);
        } else if (!imu_ok) {
            indicators_set_status(IND_STATUS_FAULT);
        } else if (!s_has_hub) {
            indicators_set_status(IND_STATUS_OK);       /* stand-alone: there is no link to miss */
        } else {
            indicators_set_status(st.link_ok ? IND_STATUS_OK : IND_STATUS_NO_NETWORK);
        }
        esp_task_wdt_reset();
    }
}

/* ---------------------------------------------------------------------------
 * Boot
 * ------------------------------------------------------------------------- */

static void IRAM_ATTR button_isr(void *arg){
    (void) arg;
    comm_event_t ev = { .kind = COMM_EV_BUTTON };
    BaseType_t hp_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_comm_queue, &ev, &hp_task_woken) != pdTRUE) {
        /* Queue full: an "I'm fine" press must not vanish. comm_task picks it up within COMM_MAX_WAIT_MS. */
        atomic_store(&s_button_missed, true);
    }
    portYIELD_FROM_ISR(hp_task_woken);
}

static void init_button(void){
    gpio_config_t button_config = {
        .pin_bit_mask = 1ULL << PIN_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,   /* pressed = shorted to GND */
    };
    ESP_ERROR_CHECK(gpio_config(&button_config));

    /* Button to GND: pull-up, falling edge and glitch filter (slide "ESP32 - GPIO"). */
    gpio_pin_glitch_filter_config_t filter_config = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = PIN_BUTTON,
    };
    gpio_glitch_filter_handle_t filter;
    ESP_ERROR_CHECK(gpio_new_pin_glitch_filter(&filter_config, &filter));
    ESP_ERROR_CHECK(gpio_glitch_filter_enable(filter));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(PIN_BUTTON, button_isr, NULL));
}

static void init_nvs(void){
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

/* The hub names a wearable by the last 3 bytes of its station MAC (docs/protocolo.md §3). */
static void read_wearable_id(void){
    uint8_t mac[6];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(s_wearable_id, sizeof(s_wearable_id), "%02x%02x%02x", mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "wearable %s, MAC " MACSTR " (configure it in the hub)", s_wearable_id, MAC2STR(mac));
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

    s_comm_queue = xQueueCreate(COMM_QUEUE_DEPTH, sizeof(comm_event_t));
    s_activity_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(s_comm_queue && s_activity_lock ? ESP_OK : ESP_ERR_NO_MEM);
#ifdef CONFIG_GADGET_FALL_CLASSIFIER_LR
    s_classifier = FP_CLASSIFIER_LR;
#else
    s_classifier = FP_CLASSIFIER_RF;
#endif
    fall_pipeline_init(&s_pipeline, CONFIG_GADGET_FALL_TRIGGER_CG / 100.0f, s_classifier);
    ESP_LOGI(TAG, "fall pipeline: trigger %.2f g, %s, fall when P >= %.2f (trained at +-%.0f g)",
             CONFIG_GADGET_FALL_TRIGGER_CG / 100.0f, fp_classifier_name(s_classifier),
             fp_classifier_threshold(s_classifier), FALL_MODEL_RANGE_G);

    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_I2C_SDA,
        .scl_io_num = PIN_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    /*
     * The driver logs every unanswered transaction as an error; an unplugged sensor is a
     * state, reported once by sampling_task, which also brings the MPU-6050 up.
     */
    esp_log_level_set("i2c.master", ESP_LOG_NONE);
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &s_i2c_bus));

#ifdef CONFIG_GADGET_BATTERY_SENSE
    esp_err_t battery_err = battery_init(PIN_BATTERY, CONFIG_GADGET_BATTERY_DIVIDER_X1000);
    if (battery_err != ESP_OK) {
        ESP_LOGW(TAG, "battery not measured: %s", esp_err_to_name(battery_err));
    }
#endif

    init_button();
    read_wearable_id();

    // ========== RADIO ==========
    s_has_hub = CONFIG_GADGET_HUB_MAC[0] != '\0';
#ifdef CONFIG_GADGET_MQTT
    const bool wants_mqtt = true;
#else
    const bool wants_mqtt = false;
#endif
    if (s_has_hub || wants_mqtt) {
        /* Without MQTT the radio only carries ESP-NOW; with it, the wearable also joins the AP. */
        net_config_t net_config = {
            .join_ap = wants_mqtt,
            .power_save = true,
        };
        ESP_ERROR_CHECK(net_init(&net_config));
    }

    if (s_has_hub) {
        espnow_tx_config_t radio = {
            .hub_mac = CONFIG_GADGET_HUB_MAC,
#ifdef CONFIG_GADGET_ESPNOW_ENCRYPT
            .encrypt = true,
#endif
            .pmk = CONFIG_GADGET_ESPNOW_PMK,
            .lmk = CONFIG_GADGET_ESPNOW_LMK,
            .fixed_channel = net_joins_ap(),
        };
        esp_err_t radio_err = espnow_tx_init(&radio);
        if (radio_err != ESP_OK) {
            /* Detection keeps running; falls go where a stand-alone wearable sends them. */
            ESP_LOGE(TAG, "ESP-NOW off (%s): falls go to the serial log%s", esp_err_to_name(radio_err),
                     wants_mqtt ? " and to MQTT" : "");
            s_has_hub = false;
        }
    } else {
        ESP_LOGW(TAG, "no hub configured (menu Wearable > MAC of the room hub): stand-alone, falls go to the "
                 "serial log%s", wants_mqtt ? " and to MQTT" : "");
    }

#ifdef CONFIG_GADGET_MQTT
    esp_err_t mqtt_err = start_mqtt();
    if (mqtt_err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT off: %s", esp_err_to_name(mqtt_err));
    }
#endif

#ifdef CONFIG_GADGET_CAN
    // ========== CAN (bench only) ==========
    can_config_t can_config = {
        .tx_gpio = PIN_CAN_TX,
        .rx_gpio = PIN_CAN_RX,
        .group = CAN_GROUP_PROJECT,
    };
    esp_err_t can_err = can_init(&can_config);
    if (can_err != ESP_OK) {
        /* A bench extra: never worth losing the fall detection over. */
        ESP_LOGW(TAG, "CAN off: %s", esp_err_to_name(can_err));
    }
#endif

    /* Core 1 belongs to the signal; the radio and the protocols stay on core 0. */
    ESP_ERROR_CHECK(esp_err_from_pdpass(xTaskCreatePinnedToCore(sampling_task, "sampling", STACK_MEDIUM, NULL,
                                                                PRIO_ALTA, NULL, CORE_REALTIME)));
    ESP_ERROR_CHECK(esp_err_from_pdpass(xTaskCreatePinnedToCore(comm_task, "comm", STACK_LARGE, NULL,
                                                                PRIO_MEDIA, NULL, CORE_PROTOCOL)));
}
