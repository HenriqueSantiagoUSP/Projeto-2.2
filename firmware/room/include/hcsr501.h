#ifndef __HCSR501_H__
#define __HCSR501_H__

#include "map_pin.h"
#include "esp_err.h"
#include "system_events.h"

#define CAN_HCSR501_ID      HCSR501_ID

#define HC_GPIO_PIN         _HCSR501_GPIO_PIN
#define HC_GPIO_MODE        GPIO_MODE_INPUT
#define HC_GPIO_PULL_UP     GPIO_PULLUP_DISABLE
#define HC_GPIO_PULL_DOWN   GPIO_PULLDOWN_DISABLE    
#define HC_GPIO_INTR        GPIO_INTR_POSEDGE

esp_err_t hcsr501_init(QueueHandle_t events_queue);

void hcsr501_task_callback();

#endif