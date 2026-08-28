#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "can.h"
#include "can_ids.h"
#include "board_pins.h"
#include "rtos_err.h"
#include "esp_log.h"

static const char *TAG = "can";

static twai_node_handle_t s_twai_node = NULL;
static QueueHandle_t s_can_rx_queue = NULL;

/*
 * Transmission is asynchronous and the driver only stores a pointer to the frame,
 * so the frame and its payload must outlive can_transmit(). Only sensors_task
 * transmits, and each call waits for the previous frame to drain first, so a single
 * slot is enough.
 */
static twai_frame_t s_tx_frame;
static uint8_t s_tx_buffer[CAN_MAX_PAYLOAD];

/*
 * RX handler registry. Written only by can_rx_handler_register() before can_init()
 * creates can_task, and read only by can_task afterwards, so it needs no locking.
 */
typedef struct {
    can_group_t        group;
    can_sensor_kind_t  kind;
    can_rx_handler_t   handler;
    void              *ctx;
} can_rx_handler_entry_t;

static can_rx_handler_entry_t s_handlers[CAN_RX_HANDLER_MAX];
static size_t s_handler_count = 0;

static bool twai_rx_callback(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx){
    uint8_t recv_buff[CAN_MAX_PAYLOAD];
    twai_frame_t rx_frame = {
        .buffer = recv_buff,
        .buffer_len = sizeof(recv_buff),
    };

    if (twai_node_receive_from_isr(handle, &rx_frame) != ESP_OK) {
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
    xQueueSendFromISR(s_can_rx_queue, &evt, &task_wakeup);

    return task_wakeup == pdTRUE;
}

/* True when a registered filter accepts a frame from this sender. info->group is
   non-NULL: both callers only get here after can_id_decode() returned true. */
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

static void can_task(void *arg){
    can_rx_event_t evt;
    can_id_info_t info;

    while (1) {
        if (xQueueReceive(s_can_rx_queue, &evt, portMAX_DELAY) != pdTRUE) {
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

esp_err_t can_rx_handler_register(can_group_t group, can_sensor_kind_t kind,
                                  can_rx_handler_t handler, void *ctx){
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
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

esp_err_t can_init(void){
    s_can_rx_queue = xQueueCreate(CAN_QUEUE_DEPTH, sizeof(can_rx_event_t));
    if (s_can_rx_queue == NULL) {
        ESP_LOGE(TAG, "could not create the rx queue");
        return ESP_ERR_NO_MEM;
    }

    twai_onchip_node_config_t node_config = {
        .io_cfg.tx =                    PIN_CAN_TX,
        .io_cfg.rx =                    PIN_CAN_RX,
        .bit_timing.bitrate =           CAN_BITRATE,
        .tx_queue_depth =               CAN_QUEUE_DEPTH,
        .flags.enable_self_test =       CAN_SELF_TEST,
        .flags.enable_loopback =        CAN_SELF_TEST,
    };

    esp_err_t err = twai_new_node_onchip(&node_config, &s_twai_node);
    if (err != ESP_OK) return err;

    twai_event_callbacks_t user_cbs = {
        .on_rx_done = twai_rx_callback,
    };
    err = twai_node_register_event_callbacks(s_twai_node, &user_cbs, NULL);
    if (err != ESP_OK) return err;

    err = twai_node_enable(s_twai_node);
    if (err != ESP_OK) return err;

    return esp_err_from_pdpass(xTaskCreate(can_task, "can_task", 4096, NULL, 10, NULL));
}

esp_err_t can_transmit(can_sensor_id_t sensor, const uint8_t *data, size_t len){
    if (data == NULL || len > CAN_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint32_t) sensor > 0xF) {
        /* Would bleed into the group nibble of the 0x0GS id. */
        return ESP_ERR_INVALID_ARG;
    }

    /*
     * The driver keeps a pointer to the frame and its buffer until the transmission
     * completes, it does not copy them, so both live in static storage here. Waiting
     * for the previous frame to drain before overwriting that storage is what keeps
     * the pointer valid. The wait returns immediately when the node is idle, and is
     * bounded so that an unacknowledged bus can never stall sensors_task.
     */
    esp_err_t err = twai_node_transmit_wait_all_done(s_twai_node, CAN_TX_DRAIN_MS);
    if (err != ESP_OK) {
        return err;
    }

    memcpy(s_tx_buffer, data, len);
    s_tx_frame = (twai_frame_t) {
        .header.id = CAN_NODE_MSG_ID(sensor),
        .header.ide = false,
        .buffer = s_tx_buffer,
        .buffer_len = len,
    };

    return twai_node_transmit(s_twai_node, &s_tx_frame, CAN_TX_TIMEOUT_MS);
}
