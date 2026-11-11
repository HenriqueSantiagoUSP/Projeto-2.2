#pragma once

/*
 * Reed switches (door contacts), one per entry of BOARD_DOORS in board_pins.h,
 * GPIO + interrupt driven, reported through sensor_events.
 */

#include <stddef.h>

#include "esp_err.h"

/* Mechanical contact: it bounces for a few ms on every actuation. The level is read
   only after this long without edges. */
#define REEDSWITCH_SETTLE_MS     50

/* Needs gpio_install_isr_service() and sensor_events_init() first. */
esp_err_t reedswitch_init(void);

/* The door ids of BOARD_DOORS, in index order; the array lives for the whole program. */
const char *const *reedswitch_ids(size_t *count);
