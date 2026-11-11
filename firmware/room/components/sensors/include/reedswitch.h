#pragma once

/*
 * Reed switches (door contacts), one per entry of BOARD_DOORS in board_pins.h,
 * GPIO + interrupt driven, reported through sensor_events.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* Mechanical contact: it bounces for a few ms on every actuation. The level is read
   only after this long without edges. */
#define REEDSWITCH_SETTLE_MS     50

/*
 * Needs gpio_install_isr_service() and sensor_events_init() first. The doors named in
 * CONFIG_HUB_DOORS_ABSENT are skipped, with one warning each.
 */
esp_err_t reedswitch_init(void);

/* The door ids of BOARD_DOORS, in index order, fitted or not; the array lives for the whole program. */
const char *const *reedswitch_ids(size_t *count);

/* False for a door listed in CONFIG_HUB_DOORS_ABSENT (valid after reedswitch_init()). */
bool reedswitch_fitted(size_t door);
