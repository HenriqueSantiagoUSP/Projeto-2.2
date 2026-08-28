#include "hcsr501.h"

static const char* TAG = "HCSR501";
static QueueHandle_t s_fila = NULL;

static void IRAM_ATTR hcsr501_isr(void *arg){
    sensor_event_t evt = {
        .type = HCSR501_EVENT,
        .timestamp = xTaskGetTickCountFromISR(),
        .gpio_level = gpio_get_level(HC_GPIO_PIN),
    };
    xQueueSendFromISR(s_fila, &evt, NULL);
}

esp_err_t hcsr501_init(QueueHandle_t events_queue){
    s_fila = events_queue;

    gpio_config_t hcsr501_config = {
        .pin_bit_mask =     1ULL << HC_GPIO_PIN,
        .mode =             HC_GPIO_MODE,
        .pull_up_en =       HC_GPIO_PULL_UP,
        .pull_down_en =     HC_GPIO_PULL_DOWN,
        .intr_type =        HC_GPIO_INTR
    };

    esp_err_t err = gpio_config(&hcsr501_config);
    if (err != ESP_OK) return err;

    return gpio_isr_handler_add(HC_GPIO_PIN, hcsr501_isr, NULL);
}

void hcsr501_task_callback(){
    const char* msg = "ON";
    
    ESP_LOGI(TAG, "%s", msg);

    esp_err_t err = can_transmit(CAN_HCSR501_ID, (const uint8_t *)msg, strlen(msg) + 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "falha ao transmitir via CAN: %s", esp_err_to_name(err));
    }
    
}