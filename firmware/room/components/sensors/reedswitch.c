#include <ctype.h>
#include <stdbool.h>
#include <string.h>

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

static bool s_fitted[DOOR_COUNT];   /* written once by reedswitch_init() */

/*
 * Marks the doors named in CONFIG_HUB_DOORS_ABSENT ("banheiro", "quarto, banheiro").
 * A name that is not in BOARD_DOORS is a typo worth a warning, never a reason to stop.
 */
static void read_absent_list(void){
    for (size_t i = 0; i < DOOR_COUNT; i++) {
        s_fitted[i] = true;
    }
    const char *p = CONFIG_HUB_DOORS_ABSENT;
    while (*p != '\0') {
        while (*p == ',' || isspace((unsigned char) *p)) {
            p++;
        }
        const char *start = p;
        while (*p != '\0' && *p != ',') {
            p++;
        }
        size_t len = (size_t) (p - start);
        while (len > 0 && isspace((unsigned char) start[len - 1])) {
            len--;
        }
        if (len == 0) {
            continue;
        }
        bool known = false;
        for (size_t i = 0; i < DOOR_COUNT; i++) {
            if (strlen(s_door_ids[i]) == len && strncmp(s_door_ids[i], start, len) == 0) {
                s_fitted[i] = false;
                known = true;
            }
        }
        if (!known) {
            ESP_LOGW(TAG, "HUB_DOORS_ABSENT names \"%.*s\", which is not a door of BOARD_DOORS", (int) len, start);
        }
    }
}

esp_err_t reedswitch_init(void){
    read_absent_list();
    for (size_t i = 0; i < DOOR_COUNT; i++) {
        if (!s_fitted[i]) {
            ESP_LOGW(TAG, "door %u \"%s\" not fitted (menuconfig): not read, no rules", (unsigned) i,
                     s_door_ids[i]);
            continue;
        }
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

bool reedswitch_fitted(size_t door){
    return door < DOOR_COUNT && s_fitted[door];
}
