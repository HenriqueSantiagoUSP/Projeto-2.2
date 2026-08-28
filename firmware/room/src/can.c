#include "can.h"

static const char *TAG = "CAN";
static twai_node_handle_t node_hdl = NULL;
static QueueHandle_t s_fila_can = NULL;

static bool twai_rx_callback(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx){
    uint8_t recv_buff[8];
    twai_frame_t rx_frame = {
        .buffer = recv_buff,
        .buffer_len = sizeof(recv_buff),
    };

    if (twai_node_receive_from_isr(handle, &rx_frame) != ESP_OK) {
        return false;
    }

    event_can_t evt = {
        .id = rx_frame.header.id,
        .tamanho = twaifd_dlc2len(rx_frame.header.dlc),
        .timestamp = xTaskGetTickCountFromISR(),
    };
    memcpy(evt.dados, recv_buff, evt.tamanho);

    BaseType_t task_wakeup = pdFALSE;
    xQueueSendFromISR(s_fila_can, &evt, &task_wakeup);

    return task_wakeup == pdTRUE;
}

static void can_task(void *arg){
    event_can_t evt;

    while (1) {
        if (xQueueReceive(s_fila_can, &evt, portMAX_DELAY)) {

            ESP_LOGI(TAG, "ID: 0x%03lX, tamanho: %d", evt.id, evt.tamanho);
            for (int i = 0; i < evt.tamanho; i++) {
                ESP_LOGI(TAG, "  byte[%d] = 0x%02X", i, evt.dados[i]);
            }
            ESP_LOGI(TAG, "%s\n", (char*) evt.dados);
            // sua lógica de tratamento aqui
        }
    }
}

esp_err_t can_init(void){
    s_fila_can = xQueueCreate(5, sizeof(event_can_t));

    twai_onchip_node_config_t node_config = {
        .io_cfg.tx =                    CAN_TX_PIN,
        .io_cfg.rx =                    CAN_RX_PIN,
        .bit_timing.bitrate =           CAN_BITRATE,
        .tx_queue_depth =               CAN_QUEUE_DEPTH,
        .flags.enable_self_test =       CAN_SELF_TEST,
        .flags.enable_loopback =        CAN_SELF_TEST,
    };
    ESP_ERROR_CHECK(twai_new_node_onchip(&node_config, &node_hdl));

    twai_event_callbacks_t user_cbs = {
        .on_rx_done = twai_rx_callback,
    };
    ESP_ERROR_CHECK(twai_node_register_event_callbacks(node_hdl, &user_cbs, NULL));

    esp_err_t err = twai_node_enable(node_hdl);
    if (err != ESP_OK) return err;

    return pd_check(xTaskCreate(can_task, "can_task", 4096, NULL, 10, NULL));
}

esp_err_t can_transmit(uint8_t sensor_id, const uint8_t *send_buff, size_t len){
    uint16_t id = CAN_GROUP_ID + sensor_id;

    twai_frame_t tx_msg = {
        .header.id = id,
        .header.ide = false,
        .buffer = (uint8_t *)send_buff,
        .buffer_len = len,
    };

    esp_err_t err = twai_node_transmit(node_hdl, &tx_msg, 0);
    if (err == ESP_ERR_TIMEOUT) {
        return err;  // fila de TX cheia, não é fatal — quem chamar decide o que fazer
    }
    ESP_ERROR_CHECK(err);

    return twai_node_transmit_wait_all_done(node_hdl, -1);
}