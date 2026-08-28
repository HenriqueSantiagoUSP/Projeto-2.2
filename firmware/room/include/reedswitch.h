#ifndef __REEDSWITCH_H__
#define __REEDSWITCH_H__

#include "map_pin.h"
#include "esp_err.h"
#include "system_events.h"

#define CAN_REEDSWITCH_ID   REEDSWITCH_ID

#define RS_GPIO_PIN         _REEDSWITCH_GPIO_PIN
#define RS_GPIO_MODE        GPIO_MODE_INPUT
#define RS_GPIO_PULL_UP     GPIO_PULLUP_ENABLE
#define RS_GPIO_PULL_DOWN   GPIO_PULLDOWN_DISABLE    
#define RS_GPIO_INTR        GPIO_INTR_ANYEDGE

esp_err_t reedswitch_init(QueueHandle_t events_queue);
void reedswitch_task_callback(sensor_event_t evt);

#endif