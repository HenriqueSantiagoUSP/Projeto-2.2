#pragma once

/* HC-SR501 PIR motion sensor, GPIO + interrupt driven. */

#include "esp_err.h"
#include "driver/gpio.h"

#include "sensor_events.h"

#define HCSR501_GPIO_MODE        GPIO_MODE_INPUT
#define HCSR501_GPIO_PULL_UP     GPIO_PULLUP_DISABLE
#define HCSR501_GPIO_PULL_DOWN   GPIO_PULLDOWN_DISABLE
/* ANYEDGE so both the start and the end of a detection are reported. */
#define HCSR501_GPIO_INTR        GPIO_INTR_ANYEDGE
#define HCSR501_DEBOUNCE_MS      20

esp_err_t hcsr501_init(QueueHandle_t events_queue);

/* Called by sensors_task once an event passed the debounce window. */
void hcsr501_handle_event(const sensor_event_t *event);
