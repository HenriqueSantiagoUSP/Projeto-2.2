#pragma once

/*
 * Local signalling: a status LED and a buzzer.
 *
 * Both run from one task, so callers only set the wanted state and never block.
 * The LED is the WS2812 soldered on the DevKitC-1 (GPIO48 on board v1.0, GPIO38 on
 * v1.1); the buzzer is an active buzzer switched by a GPIO. Either can be left out
 * with GPIO_NUM_NC.
 */

#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

typedef enum {
    IND_STATUS_BOOT,         /* steady blue */
    IND_STATUS_OK,           /* short green flash at 1 Hz */
    IND_STATUS_NO_NETWORK,   /* short yellow flash at 1 Hz */
    IND_STATUS_ALERT,        /* red blinking at 4 Hz */
    IND_STATUS_FAULT,        /* slow red: 500 ms on every 2 s, an essential sensor is missing */
} ind_status_t;

typedef struct {
    gpio_num_t led_gpio;
    gpio_num_t buzzer_gpio;
} indicators_config_t;

esp_err_t indicators_init(const indicators_config_t *config);

void indicators_set_status(ind_status_t status);

/* Beeps (250 ms on, 250 ms off) for duration_ms, or until indicators_buzzer_stop() when 0. */
void indicators_buzzer_start(uint32_t duration_ms);
void indicators_buzzer_stop(void);
