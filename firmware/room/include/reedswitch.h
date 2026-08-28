#pragma once

/* Reed switch (door/window contact), GPIO + interrupt driven. */

#include "esp_err.h"
#include "driver/gpio.h"

#include "sensor_events.h"

#define REEDSWITCH_GPIO_MODE        GPIO_MODE_INPUT
#define REEDSWITCH_GPIO_PULL_UP     GPIO_PULLUP_ENABLE
#define REEDSWITCH_GPIO_PULL_DOWN   GPIO_PULLDOWN_DISABLE
#define REEDSWITCH_GPIO_INTR        GPIO_INTR_ANYEDGE
/* Mechanical contact: bounces for a few ms on every actuation. */
#define REEDSWITCH_DEBOUNCE_MS      500

esp_err_t reedswitch_init(QueueHandle_t events_queue);

/* Called by sensors_task once an event passed the debounce window. */
void reedswitch_handle_event(const sensor_event_t *event);
