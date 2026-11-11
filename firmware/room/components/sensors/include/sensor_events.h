#pragma once

/*
 * Sensor event bus of the room hub.
 *
 * Digital inputs (PIR, doors) interrupt on every edge, and their ISR only queues
 * the input slot. sensors_task then waits until that input has been quiet for its
 * settle time, reads the now stable level and reports it only if it differs from
 * the last report. Reading after the bounce, not at the first edge, is what keeps a
 * door from being reported in whatever state it had mid-bounce, and a quick
 * open-close from being lost.
 *
 * Analog readings (LM35) come from their own task and go out through the same
 * listener. The listener runs in the producer's task, so it must be quick and
 * thread safe: the hub only queues the reading.
 *
 * This header must not include the individual sensor headers: the drivers depend
 * on the bus, not the other way around.
 */

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "esp_err.h"

#define SENSOR_INPUT_MAX    9   /* the PIR and up to 8 doors */

typedef enum {
    SENSOR_KIND_PRESENCE,
    SENSOR_KIND_DOOR,
    SENSOR_KIND_TEMPERATURE,
} sensor_kind_t;

typedef struct {
    sensor_kind_t kind;
    uint8_t       index;     /* door number for SENSOR_KIND_DOOR, 0 otherwise */
    bool          active;    /* presence detected / door open */
    float         celsius;   /* SENSOR_KIND_TEMPERATURE only */
} sensor_reading_t;

typedef void (*sensor_listener_t)(const sensor_reading_t *reading, void *ctx);

/* Starts the bus. Call before the drivers register their inputs. */
esp_err_t sensor_events_init(sensor_listener_t listener, void *ctx);

/*
 * Registers an already configured input GPIO and attaches its ISR.
 * active_level is the level that means presence / open. Nothing is reported before
 * ignore_ms has passed (PIR warm-up); the first report is the stable level at the
 * end of it, so the hub always learns the starting state of every input.
 */
esp_err_t sensor_events_add_input(gpio_num_t gpio, sensor_kind_t kind, uint8_t index,
                                  int active_level, uint32_t settle_ms, uint32_t ignore_ms);

/* Delivers a reading produced outside the bus (analog sensors). */
void sensor_events_emit(const sensor_reading_t *reading);
