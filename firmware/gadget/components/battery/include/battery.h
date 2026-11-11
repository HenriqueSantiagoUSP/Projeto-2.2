#pragma once

/* Battery voltage of the wearable, read by ADC1 through a resistive divider. */

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

/* divider_x1000: battery voltage / pin voltage, times 1000 (2000 for two equal resistors). */
esp_err_t battery_init(gpio_num_t pin, uint32_t divider_x1000);

/* Battery voltage in mV, or ESPNOW_BATTERY_UNKNOWN when it cannot be measured. */
uint16_t battery_read_mv(void);
