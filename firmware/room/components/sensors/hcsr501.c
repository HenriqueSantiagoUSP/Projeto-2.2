#include "hcsr501.h"
#include "sensor_events.h"
#include "board_pins.h"
#include "esp_log.h"

static const char *TAG = "hcsr501";

esp_err_t hcsr501_init(void){
    gpio_config_t hcsr501_config = {
        .pin_bit_mask =     1ULL << PIN_HCSR501,
        .mode =             GPIO_MODE_INPUT,
        .pull_up_en =       HCSR501_GPIO_PULL_UP,
        .pull_down_en =     HCSR501_GPIO_PULL_DOWN,
        /* Both edges: the start and the end of a detection are both reported. */
        .intr_type =        GPIO_INTR_ANYEDGE,
    };

    esp_err_t err = gpio_config(&hcsr501_config);
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG, "warming up for %d s before the first reading", HCSR501_WARMUP_MS / 1000);
    return sensor_events_add_input(PIN_HCSR501, SENSOR_KIND_PRESENCE, 0, 1,
                                   HCSR501_SETTLE_MS, HCSR501_WARMUP_MS);
}
