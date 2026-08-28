#pragma once

/*
 * Sensor event bus.
 *
 * Sensor ISRs post a sensor_event_t into a shared queue; sensors_task drains it,
 * applies a per-sensor debounce window and dispatches to the sensor handler.
 *
 * This header must not include the individual sensor headers: the sensors depend
 * on the event type, not the other way around. Only the dispatcher (sensor_events.c)
 * knows about the concrete sensors.
 */

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#define SENSOR_EVENT_QUEUE_DEPTH   16

typedef enum {
    SENSOR_EVENT_HCSR501,
    SENSOR_EVENT_REEDSWITCH,
    SENSOR_EVENT_COUNT,          /* sizes the debounce table, keep last */
} sensor_event_type_t;

typedef struct {
    sensor_event_type_t type;
    uint32_t            timestamp;   /* tick count sampled in the ISR */
    bool                level;       /* GPIO level sampled in the ISR, may still be bouncing */
} sensor_event_t;

/* Creates the dispatcher task that drains events_queue. */
esp_err_t sensors_task_init(QueueHandle_t events_queue);
