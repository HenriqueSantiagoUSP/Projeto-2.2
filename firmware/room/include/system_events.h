#ifndef __SYSTEM_EVENTS_H__
#define __SYSTEM_EVENTS_H__

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"

#include "hcsr501.h"
#include "reedswitch.h"

typedef enum {
    REEDSWITCH_EVENT,
    HCSR501_EVENT,
} event_type_t;

typedef struct {
    event_type_t type;
    uint32_t timestamp;
    bool gpio_level;
} sensor_event_t;

esp_err_t sensors_task_init(QueueHandle_t events_queue);
esp_err_t pd_check(BaseType_t arg);

#endif