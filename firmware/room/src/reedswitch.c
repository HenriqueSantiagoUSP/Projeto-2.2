#include "reedswitch.h"

static const char* TAG = "REEDSWITCH";
static QueueHandle_t s_fila = NULL;

static void IRAM_ATTR reedswitch_isr(void *arg){
    sensor_event_t evt = {
        .type = REEDSWITCH_EVENT,
        .timestamp = xTaskGetTickCountFromISR(),
        .gpio_level = gpio_get_level(RS_GPIO_PIN),
    };
    xQueueSendFromISR(s_fila, &evt, NULL);
}

esp_err_t reedswitch_init(QueueHandle_t events_queue){
    s_fila = events_queue;

    gpio_config_t reedswitch_config = {
        .pin_bit_mask =     1ULL << RS_GPIO_PIN,
        .mode =             RS_GPIO_MODE,
        .pull_up_en =       RS_GPIO_PULL_UP,
        .pull_down_en =     RS_GPIO_PULL_DOWN,
        .intr_type =        RS_GPIO_INTR
    };

    esp_err_t err = gpio_config(&reedswitch_config);
    if (err != ESP_OK) return err;

    return gpio_isr_handler_add(RS_GPIO_PIN, reedswitch_isr, NULL);
}

void reedswitch_task_callback(sensor_event_t evt){
    const char *msg = (evt.gpio_level == 0) ? "CLOSE" : "OPEN";

    ESP_LOGI(TAG, "%s", msg);

    esp_err_t err = can_transmit(CAN_REEDSWITCH_ID, (const uint8_t *)msg, strlen(msg) + 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "falha ao transmitir via CAN: %s", esp_err_to_name(err));
    }
    
}