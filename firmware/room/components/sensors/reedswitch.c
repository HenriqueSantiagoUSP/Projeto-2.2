#include "reedswitch.h"
#include "sensor_events.h"
#include "board_pins.h"
#include "esp_log.h"

static const char *TAG = "reedswitch";

#define DOOR_GPIO(index, gpio, id, role)   gpio,
#define DOOR_ID(index, gpio, id, role)     id,

static const gpio_num_t s_door_gpios[] = { BOARD_DOORS(DOOR_GPIO) };
static const char *const s_door_ids[] = { BOARD_DOORS(DOOR_ID) };

#define DOOR_COUNT   (sizeof(s_door_gpios) / sizeof(s_door_gpios[0]))

esp_err_t reedswitch_init(void){
    for (size_t i = 0; i < DOOR_COUNT; i++) {
        gpio_config_t door_config = {
            .pin_bit_mask =     1ULL << s_door_gpios[i],
            .mode =             GPIO_MODE_INPUT,
            /* The switch shorts to GND when the door is shut: no external resistor. */
            .pull_up_en =       GPIO_PULLUP_ENABLE,
            .pull_down_en =     GPIO_PULLDOWN_DISABLE,
            .intr_type =        GPIO_INTR_ANYEDGE,
        };

        esp_err_t err = gpio_config(&door_config);
        if (err != ESP_OK) return err;

        err = sensor_events_add_input(s_door_gpios[i], SENSOR_KIND_DOOR, (uint8_t) i, 1,
                                      REEDSWITCH_SETTLE_MS, 0);
        if (err != ESP_OK) return err;

        ESP_LOGI(TAG, "door %u \"%s\" on GPIO%d", (unsigned) i, s_door_ids[i], s_door_gpios[i]);
    }
    return ESP_OK;
}

const char *const *reedswitch_ids(size_t *count){
    if (count != NULL) {
        *count = DOOR_COUNT;
    }
    return s_door_ids;
}
