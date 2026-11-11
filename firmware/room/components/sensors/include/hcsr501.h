#pragma once

/* HC-SR501 PIR motion sensor, GPIO + interrupt driven, reported through sensor_events. */

#include "esp_err.h"

/* The module drives its output push-pull; the pull-down only matters if it is unplugged,
   and then it reads as "nobody", which the inactivity rule eventually flags. */
#define HCSR501_GPIO_PULL_UP     GPIO_PULLUP_DISABLE
#define HCSR501_GPIO_PULL_DOWN   GPIO_PULLDOWN_ENABLE
/* The output is clean, so the settle time only filters glitches on the wire. */
#define HCSR501_SETTLE_MS        50
/* The pyroelectric element needs about a minute after power-up before its output means anything. */
#define HCSR501_WARMUP_MS        60000

/* Needs gpio_install_isr_service() and sensor_events_init() first. */
esp_err_t hcsr501_init(void);
