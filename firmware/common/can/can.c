#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "can.h"
#include "can_ids.h"
#include "rtos_err.h"
#include "sys_events.h"
#include "task_config.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"

static const char *TAG = "can";

#define CAN_TX_QUEUE_DEPTH      16      /* frames waiting for can_tx */
#define CAN_RX_QUEUE_DEPTH      16      /* frames waiting for can_rx */
/* A frame takes well under 1 ms at 250 kbit/s; still pending after this, nobody ACKed it. */
#define CAN_TX_DONE_MS          20
/*
 * Pause before leaving bus-off, so a broken bus does not keep the node toggling. It
 * doubles while the bus stays unreachable (no transceiver, no cable), up to a minute.
 */
#define CAN_RECOVER_MIN_MS      1000
#define CAN_RECOVER_MAX_MS      60000
#define CAN_DROP_LOG_MS         10000

/*
 * What can_tx knows about the bus, so a missing one is reported once and not on every
 * recovery attempt. Only can_tx touches it.
 */
typedef struct {
    bool     no_bus;              /* bus-off reported: quiet until a frame really goes out again */
    bool     no_ack;              /* error passive, nobody acknowledges: reported once */
    bool     confirming;          /* recovered from bus-off, waiting for a frame to prove the bus */
    uint32_t recover_delay_ms;
} can_link_t;

/* Task notification bits of can_tx. */
#define CAN_NOTIFY_FRAME        (1u << 0)
#define CAN_NOTIFY_STATE        (1u << 1)

/* One frame waiting for can_tx, by value: the caller's buffer may be gone by then. */
typedef struct {
    uint8_t sensor;
    uint8_t length;
    uint8_t data[CAN_MAX_PAYLOAD];
} can_tx_item_t;

static twai_node_handle_t s_node = NULL;
static QueueHandle_t s_rx_queue = NULL;
static QueueHandle_t s_tx_queue = NULL;
static TaskHandle_t s_tx_task = NULL;
static uint8_t s_group;

/*
 * The driver keeps a pointer to the frame being sent, it does not copy it, so the
 * frame lives here. Only can_tx touches it, and only after the previous frame has
 * left the controller.
 */
static twai_frame_t s_tx_frame;
static uint8_t s_tx_buffer[CAN_MAX_PAYLOAD];

/*
 * RX handler registry. Written only by can_rx_handler_register() before can_init()
 * creates can_rx, and read only by can_rx afterwards, so it needs no locking.
 */
typedef struct {
    can_group_t        group;
    can_sensor_kind_t  kind;
    can_rx_handler_t   handler;
    void              *ctx;
} can_rx_handler_entry_t;

static can_rx_handler_entry_t s_handlers[CAN_RX_HANDLER_MAX];
static size_t s_handler_count = 0;

/* ---------------------------------------------------------------------------
 * Driver callbacks (ISR context): copy or flag, and get out
 * ------------------------------------------------------------------------- */

static bool IRAM_ATTR on_rx_done(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx){
    (void) edata;
    (void) user_ctx;
    uint8_t recv_buff[CAN_MAX_PAYLOAD];
    twai_frame_t rx_frame = {
        .buffer = recv_buff,
        .buffer_len = sizeof(recv_buff),
    };

    if (twai_node_receive_from_isr(handle, &rx_frame) != ESP_OK) {
        return false;
    }
    /* A remote request carries a DLC but no data: the buffer was never written. Nobody here asks for one. */
    if (rx_frame.header.rtr) {
        return false;
    }

    /* dlc2len can report more than CAN_MAX_PAYLOAD for CAN-FD DLCs, so clamp it. */
    size_t len = twaifd_dlc2len(rx_frame.header.dlc);
    if (len > sizeof(recv_buff)) {
        len = sizeof(recv_buff);
    }

    can_rx_event_t evt = {
        .id = rx_frame.header.id,
        .length = (uint8_t) len,
        .timestamp = xTaskGetTickCountFromISR(),
    };
    memcpy(evt.data, recv_buff, len);

    BaseType_t task_wakeup = pdFALSE;
    xQueueSendFromISR(s_rx_queue, &evt, &task_wakeup);
    return task_wakeup == pdTRUE;
}

static bool IRAM_ATTR on_state_change(twai_node_handle_t handle, const twai_state_change_event_data_t *edata, void *user_ctx){
    (void) handle;
    (void) edata;
    (void) user_ctx;
    /* Only a flag: can_tx reads the current state itself. A notification is never lost, unlike a full queue. */
    BaseType_t task_wakeup = pdFALSE;
    xTaskNotifyFromISR(s_tx_task, CAN_NOTIFY_STATE, eSetBits, &task_wakeup);
    return task_wakeup == pdTRUE;
}

/* ---------------------------------------------------------------------------
 * can_rx: decode and dispatch
 * ------------------------------------------------------------------------- */

/* True when a registered filter accepts a frame from this sender. info->group is
   non-NULL: the only caller gets here after can_id_decode() returned true. */
static bool handler_matches(const can_rx_handler_entry_t *entry, const can_id_info_t *info){
    if (entry->group != CAN_GROUP_ANY && entry->group != info->group->group) {
        return false;
    }
    if (entry->kind == CAN_KIND_ANY) {
        return true;
    }
    /* A kind filter cannot match a sensor we could not resolve. */
    return info->sensor != NULL && entry->kind == info->sensor->kind;
}

static void can_dispatch(const can_rx_event_t *evt, const can_id_info_t *info){
    for (size_t i = 0; i < s_handler_count; i++) {
        if (handler_matches(&s_handlers[i], info)) {
            s_handlers[i].handler(evt, info, s_handlers[i].ctx);
        }
    }
}

static void can_rx_task(void *arg){
    (void) arg;
    can_rx_event_t evt;
    can_id_info_t info;

    while (1) {
        if (xQueueReceive(s_rx_queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (!can_id_decode(evt.id, &info)) {
            ESP_LOGW(TAG, "frame from outside this network, id 0x%03lX", evt.id);
            continue;
        }

        if (info.sensor == NULL) {
            /* Known node, sensor we have no entry for: it published something new. */
            ESP_LOGW(TAG, "%s: unknown sensor 0x%X (id 0x%03lX)",
                     info.group->name, (unsigned) CAN_ID_SENSOR(evt.id), evt.id);
            can_dispatch(&evt, &info);
            continue;
        }

        uint8_t expected = can_kind_payload_len(info.sensor->kind);
        if (expected != 0 && evt.length != expected) {
            ESP_LOGW(TAG, "%s/%s: expected %u byte(s), got %u",
                     info.group->name, info.sensor->name, expected, evt.length);
        }

        if (evt.length > 0) {
            ESP_LOGI(TAG, "%s/%s: %u byte(s), data[0] = 0x%02X",
                     info.group->name, info.sensor->name, evt.length, evt.data[0]);
        } else {
            ESP_LOGI(TAG, "%s/%s: empty frame", info.group->name, info.sensor->name);
        }

        can_dispatch(&evt, &info);
    }
}

/* ---------------------------------------------------------------------------
 * can_tx: owns transmission and the node state
 * ------------------------------------------------------------------------- */

static const char *state_name(twai_error_state_t state){
    switch (state) {
        case TWAI_ERROR_ACTIVE:  return "error active";
        case TWAI_ERROR_WARNING: return "error warning";
        case TWAI_ERROR_PASSIVE: return "error passive";
        case TWAI_ERROR_BUS_OFF: return "bus-off";
        default:                 return "?";
    }
}

/*
 * Reads the node state after a change; returns when to start a recovery (0 = none).
 * A missing bus is a state, not an error: one warning when it is first seen, quiet
 * retries with a growing pause, and one line when it is back.
 */
static int64_t handle_state_change(int64_t recover_at_us, can_link_t *link){
    twai_node_status_t status;
    if (twai_node_get_info(s_node, &status, NULL) != ESP_OK) {
        return recover_at_us;
    }

    switch (status.state) {
        case TWAI_ERROR_BUS_OFF: {
            sys_events_clear(SYS_EV_CAN_RUNNING);
            sys_events_set(SYS_EV_CAN_BUS_OFF);
            link->confirming = false;
            if (!link->no_bus) {
                ESP_LOGW(TAG, "CAN bus not connected (no transceiver, cable or other node?): carrying on "
                         "without CAN, retrying every %d s at most", CAN_RECOVER_MAX_MS / 1000);
                link->no_bus = true;
            } else {
                ESP_LOGD(TAG, "still bus-off (TEC %u, REC %u), next try in %lu ms", status.tx_error_count,
                         status.rx_error_count, (unsigned long) link->recover_delay_ms);
            }
            int64_t at = esp_timer_get_time() + (int64_t) link->recover_delay_ms * 1000;
            link->recover_delay_ms = link->recover_delay_ms * 2 > CAN_RECOVER_MAX_MS ? CAN_RECOVER_MAX_MS
                                                                                      : link->recover_delay_ms * 2;
            return at;
        }

        case TWAI_ERROR_ACTIVE:
            sys_events_clear(SYS_EV_CAN_BUS_OFF);
            sys_events_set(SYS_EV_CAN_RUNNING);
            if (link->no_bus) {
                /*
                 * A recovery always ends here, bus or no bus: without a transceiver the next frame
                 * goes bus-off again. Stay quiet, keep the growing pause, and let can_tx_task call
                 * it connected once a frame really goes out (confirm_bus).
                 */
                link->confirming = true;
                return 0;
            }
            if (link->no_ack) {
                ESP_LOGI(TAG, "CAN bus connected again");
            }
            link->no_ack = false;
            link->recover_delay_ms = CAN_RECOVER_MIN_MS;
            return 0;

        case TWAI_ERROR_PASSIVE:
            /* With a transceiver but no other node, nothing ACKs: the node stays here for good. */
            if (!link->no_bus && !link->no_ack) {
                ESP_LOGW(TAG, "no other CAN node answers (error passive): carrying on, frames are dropped");
                link->no_ack = true;
            }
            return recover_at_us;

        default:
            /* Warning still transmits normally: a step on the way, not worth more than a debug line. */
            ESP_LOGD(TAG, "%s (TEC %u, REC %u)", state_name(status.state), status.tx_error_count,
                     status.rx_error_count);
            return recover_at_us;
    }
}

/*
 * After a bus-off recovery: once the frames just sent are done, the node still error active
 * with no transmit errors means they went out and the bus is really back.
 */
static void confirm_bus(can_link_t *link){
    if (twai_node_transmit_wait_all_done(s_node, CAN_TX_DONE_MS) != ESP_OK) {
        return;
    }
    twai_node_status_t status;
    if (twai_node_get_info(s_node, &status, NULL) != ESP_OK) {
        return;
    }
    if (status.state == TWAI_ERROR_ACTIVE && status.tx_error_count == 0) {
        ESP_LOGI(TAG, "CAN bus connected again");
        link->no_bus = false;
        link->no_ack = false;
        link->confirming = false;
        link->recover_delay_ms = CAN_RECOVER_MIN_MS;
    }
}

/* Hands one frame to the driver. False when it had to be dropped. */
static bool send_one(const can_tx_item_t *item){
    if (!can_running()) {
        return false;
    }
    /*
     * Wait for the previous frame to leave before reusing its storage. It returns at
     * once on an idle node, and gives up quickly on a frame nobody acknowledges.
     */
    if (twai_node_transmit_wait_all_done(s_node, CAN_TX_DONE_MS) != ESP_OK) {
        return false;
    }

    memcpy(s_tx_buffer, item->data, item->length);
    s_tx_frame = (twai_frame_t) {
        .header.id = CAN_MSG_ID(s_group, item->sensor),
        .header.ide = false,
        .buffer = s_tx_buffer,
        .buffer_len = item->length,
    };
    return twai_node_transmit(s_node, &s_tx_frame, 0) == ESP_OK;
}

static void can_tx_task(void *arg){
    (void) arg;
    int64_t recover_at_us = 0;
    uint32_t dropped = 0;
    int64_t last_drop_log_us = 0;
    can_link_t link = { .recover_delay_ms = CAN_RECOVER_MIN_MS };

    while (1) {
        TickType_t wait = portMAX_DELAY;
        if (recover_at_us != 0) {
            int64_t left_ms = (recover_at_us - esp_timer_get_time()) / 1000;
            wait = left_ms > 0 ? pdMS_TO_TICKS(left_ms) + 1 : 0;
        }

        uint32_t notified = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notified, wait);

        if (notified & CAN_NOTIFY_STATE) {
            recover_at_us = handle_state_change(recover_at_us, &link);
        }
        if (recover_at_us != 0 && esp_timer_get_time() >= recover_at_us) {
            recover_at_us = 0;
            /* Completes after 128 x 11 recessive bits; the state change says when. */
            esp_err_t err = twai_node_recover(s_node);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "recovery not started: %s", esp_err_to_name(err));
            }
        }

        can_tx_item_t item;
        bool sent = false;
        while (xQueueReceive(s_tx_queue, &item, 0) == pdTRUE) {
            if (send_one(&item)) {
                sent = true;
            } else {
                dropped++;
            }
        }
        if (sent && link.confirming) {
            confirm_bus(&link);
        }

        /* Drops without a known reason are worth a line; with the bus missing they are expected. */
        int64_t now_us = esp_timer_get_time();
        if (dropped > 0 && now_us - last_drop_log_us >= (int64_t) CAN_DROP_LOG_MS * 1000) {
            if (link.no_bus || link.no_ack) {
                ESP_LOGD(TAG, "%lu frame(s) dropped while the bus is missing", (unsigned long) dropped);
            } else {
                ESP_LOGW(TAG, "%lu frame(s) dropped", (unsigned long) dropped);
            }
            dropped = 0;
            last_drop_log_us = now_us;
        }
    }
}

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------- */

esp_err_t can_rx_handler_register(can_group_t group, can_sensor_kind_t kind,
                                  can_rx_handler_t handler, void *ctx){
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_node != NULL) {
        return ESP_ERR_INVALID_STATE;   /* can_rx is already reading the registry */
    }
    if (s_handler_count >= CAN_RX_HANDLER_MAX) {
        ESP_LOGE(TAG, "handler registry full (%d)", CAN_RX_HANDLER_MAX);
        return ESP_ERR_NO_MEM;
    }

    s_handlers[s_handler_count++] = (can_rx_handler_entry_t) {
        .group = group,
        .kind = kind,
        .handler = handler,
        .ctx = ctx,
    };
    return ESP_OK;
}

esp_err_t can_init(const can_config_t *config){
    if (config == NULL || config->group > 0xF) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_node != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_group = config->group;

    s_rx_queue = xQueueCreate(CAN_RX_QUEUE_DEPTH, sizeof(can_rx_event_t));
    s_tx_queue = xQueueCreate(CAN_TX_QUEUE_DEPTH, sizeof(can_tx_item_t));
    if (s_rx_queue == NULL || s_tx_queue == NULL) {
        ESP_LOGE(TAG, "could not create the queues");
        return ESP_ERR_NO_MEM;
    }

#ifdef CONFIG_APP_CAN_SELF_TEST
    const bool self_test = true;
#else
    const bool self_test = false;
#endif
    twai_onchip_node_config_t node_config = {
        .io_cfg.tx =                    config->tx_gpio,
        .io_cfg.rx =                    config->rx_gpio,
        /* Unused outputs must say so: 0 would route them to GPIO0, a strapping pin. */
        .io_cfg.quanta_clk_out =        GPIO_NUM_NC,
        .io_cfg.bus_off_indicator =     GPIO_NUM_NC,
        .bit_timing.bitrate =           CAN_BITRATE,
        /* One frame in flight at a time (see send_one); the queue in front is ours. */
        .tx_queue_depth =               1,
        /* Retransmit until acknowledged, as CAN does; can_tx drops what cannot get out. */
        .fail_retry_cnt =               -1,
        .flags.enable_self_test =       self_test,
        .flags.enable_loopback =        self_test,
    };

    esp_err_t err = twai_new_node_onchip(&node_config, &s_node);
    if (err != ESP_OK) return err;

    /* Hardware filter: only 0x0GS ids (0x000-0x0FF) reach the ISR. Must be set while disabled. */
    twai_mask_filter_config_t filter = {
        .id = 0x000,
        .mask = 0x700,
        .is_ext = false,
    };
    err = twai_node_config_mask_filter(s_node, 0, &filter);
    if (err != ESP_OK) return err;

    twai_event_callbacks_t callbacks = {
        .on_rx_done = on_rx_done,
        .on_state_change = on_state_change,
    };
    err = twai_node_register_event_callbacks(s_node, &callbacks, NULL);
    if (err != ESP_OK) return err;

    /* Both tasks exist before the node can raise an interrupt that names them. */
    err = esp_err_from_pdpass(xTaskCreatePinnedToCore(can_rx_task, "can_rx", STACK_MEDIUM, NULL,
                                                      PRIO_MEDIA, NULL, CORE_PROTOCOL));
    if (err != ESP_OK) return err;
    err = esp_err_from_pdpass(xTaskCreatePinnedToCore(can_tx_task, "can_tx", STACK_SMALL, NULL,
                                                      PRIO_MEDIA, &s_tx_task, CORE_PROTOCOL));
    if (err != ESP_OK) return err;

    err = twai_node_enable(s_node);
    if (err != ESP_OK) return err;
    sys_events_set(SYS_EV_CAN_RUNNING);

    ESP_LOGI(TAG, "group 0x%X at %d bit/s on TX GPIO%d / RX GPIO%d%s", s_group, CAN_BITRATE,
             config->tx_gpio, config->rx_gpio, self_test ? ", bench mode (self test + loopback)" : "");
    return ESP_OK;
}

esp_err_t can_transmit(uint8_t sensor, const uint8_t *data, size_t len){
    if (s_tx_task == NULL) {
        return ESP_ERR_INVALID_STATE;   /* can_init() failed or was not called */
    }
    if ((data == NULL && len > 0) || len > CAN_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    if (sensor > 0xF) {
        /* Would bleed into the group nibble of the 0x0GS id. */
        return ESP_ERR_INVALID_ARG;
    }
    if (!can_running()) {
        return ESP_ERR_INVALID_STATE;
    }

    can_tx_item_t item = {
        .sensor = sensor,
        .length = (uint8_t) len,
    };
    if (len > 0) {
        memcpy(item.data, data, len);
    }
    if (xQueueSend(s_tx_queue, &item, 0) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    xTaskNotify(s_tx_task, CAN_NOTIFY_FRAME, eSetBits);
    return ESP_OK;
}

bool can_running(void){
    return sys_events_all(SYS_EV_CAN_RUNNING);
}
