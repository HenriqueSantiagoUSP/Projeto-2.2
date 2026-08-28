#include <string.h>

#include "can.h"
#include "board_pins.h"
#include "sensor_events.h"
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

static void can_task(void *arg){
    can_rx_event_t evt;

    while (1) {
        if (xQueueReceive(s_can_rx_queue, &evt, portMAX_DELAY)) {

            ESP_LOGI(TAG, "ID: 0x%03lX, length: %d", evt.id, evt.length);
            for (int i = 0; i < evt.length; i++) {
                ESP_LOGI(TAG, "  byte[%d] = 0x%02X", i, evt.data[i]);
            }
            /* handling logic goes here */
        }
    }
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
        .header.id = CAN_MSG_ID(sensor),
        .header.ide = false,
        .buffer = s_tx_buffer,
        .buffer_len = len,
    };

    return twai_node_transmit(s_twai_node, &s_tx_frame, CAN_TX_TIMEOUT_MS);
}
