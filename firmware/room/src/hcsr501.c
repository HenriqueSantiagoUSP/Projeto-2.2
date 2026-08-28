#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "hcsr501.h"
#include "board_pins.h"
#include "can.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "hcsr501";

static QueueHandle_t s_event_queue = NULL;
static volatile uint32_t s_dropped_events = 0;
static uint32_t s_reported_drops = 0;

static void IRAM_ATTR hcsr501_isr(void *arg){
    sensor_event_t evt = {
        .type = SENSOR_EVENT_HCSR501,
        .timestamp = xTaskGetTickCountFromISR(),
        .level = gpio_get_level(PIN_HCSR501),
    };

    BaseType_t hp_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_event_queue, &evt, &hp_task_woken) != pdTRUE) {
        s_dropped_events++;   /* reported by the task, never log from an ISR */
    }
    portYIELD_FROM_ISR(hp_task_woken);
}

esp_err_t hcsr501_init(QueueHandle_t events_queue){
    if (events_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_event_queue = events_queue;

    gpio_config_t hcsr501_config = {
        .pin_bit_mask =     1ULL << PIN_HCSR501,
        .mode =             HCSR501_GPIO_MODE,
        .pull_up_en =       HCSR501_GPIO_PULL_UP,
        .pull_down_en =     HCSR501_GPIO_PULL_DOWN,
        .intr_type =        HCSR501_GPIO_INTR
    };

    esp_err_t err = gpio_config(&hcsr501_config);
    if (err != ESP_OK) return err;

    return gpio_isr_handler_add(PIN_HCSR501, hcsr501_isr, NULL);
}

void hcsr501_handle_event(const sensor_event_t *event){
    (void) event;   /* the level is re-read below, after the debounce window */

    uint32_t dropped = s_dropped_events;
    if (dropped != s_reported_drops) {
        ESP_LOGW(TAG, "%lu event(s) dropped, queue full", dropped - s_reported_drops);
        s_reported_drops = dropped;
    }

    uint8_t state = gpio_get_level(PIN_HCSR501) ? 1 : 0;

    ESP_LOGI(TAG, "%s", state ? "ON" : "OFF");

    esp_err_t err = can_transmit(CAN_SENSOR_HCSR501, &state, sizeof(state));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CAN transmission failed: %s", esp_err_to_name(err));
    }
}
