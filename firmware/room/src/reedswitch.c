#include "reedswitch.h"
#include "board_pins.h"
#include "can.h"
#include "esp_log.h"

static const char *TAG = "reedswitch";

static QueueHandle_t s_event_queue = NULL;
static volatile uint32_t s_dropped_events = 0;
static uint32_t s_reported_drops = 0;

static void IRAM_ATTR reedswitch_isr(void *arg){
    sensor_event_t evt = {
        .type = SENSOR_EVENT_REEDSWITCH,
        .timestamp = xTaskGetTickCountFromISR(),
        .level = gpio_get_level(PIN_REEDSWITCH),
    };

    BaseType_t hp_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_event_queue, &evt, &hp_task_woken) != pdTRUE) {
        s_dropped_events++;   /* reported by the task, never log from an ISR */
    }
    portYIELD_FROM_ISR(hp_task_woken);
}

esp_err_t reedswitch_init(QueueHandle_t events_queue){
    if (events_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_event_queue = events_queue;

    gpio_config_t reedswitch_config = {
        .pin_bit_mask =     1ULL << PIN_REEDSWITCH,
        .mode =             REEDSWITCH_GPIO_MODE,
        .pull_up_en =       REEDSWITCH_GPIO_PULL_UP,
        .pull_down_en =     REEDSWITCH_GPIO_PULL_DOWN,
        .intr_type =        REEDSWITCH_GPIO_INTR
    };

    esp_err_t err = gpio_config(&reedswitch_config);
    if (err != ESP_OK) return err;

    return gpio_isr_handler_add(PIN_REEDSWITCH, reedswitch_isr, NULL);
}

void reedswitch_handle_event(const sensor_event_t *event){
    (void) event;   /* the level is re-read below, after the debounce window */

    uint32_t dropped = s_dropped_events;
    if (dropped != s_reported_drops) {
        ESP_LOGW(TAG, "%lu event(s) dropped, queue full", dropped - s_reported_drops);
        s_reported_drops = dropped;
    }

    uint8_t state = gpio_get_level(PIN_REEDSWITCH) ? 1 : 0;

    ESP_LOGI(TAG, "%s", state ? "OPEN" : "CLOSE");

    esp_err_t err = can_transmit(CAN_SENSOR_REEDSWITCH, &state, sizeof(state));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "CAN transmission failed: %s", esp_err_to_name(err));
    }
}
