#pragma once

/*
 * System-wide state flags in one FreeRTOS event group (aulas/, slide "ESP32 - RTOS").
 *
 * Each bit has a single writer (the component that owns that state); any task may
 * test the bits or block on them with a timeout. This replaces the volatile bools
 * that one task wrote and others read.
 */

#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"

#define SYS_EV_NET_CONNECTED    (1u << 0)   /* net: the station has an IP */
#define SYS_EV_TIME_VALID       (1u << 1)   /* net: SNTP set the clock at least once */
#define SYS_EV_MQTT_CONNECTED   (1u << 2)   /* mqtt_link: session with the broker */
#define SYS_EV_CAN_RUNNING      (1u << 3)   /* can: node enabled and not bus-off */
#define SYS_EV_CAN_BUS_OFF      (1u << 4)   /* can: bus-off, recovery under way */

/* Creates the group. Call first thing in app_main, before any component that uses it. */
esp_err_t sys_events_init(void);

void sys_events_set(EventBits_t bits);
void sys_events_clear(EventBits_t bits);

/* True when every bit of `bits` is set. Never blocks. */
bool sys_events_all(EventBits_t bits);

/* Waits up to `timeout` for every bit of `bits`; true when they are all set. */
bool sys_events_wait_all(EventBits_t bits, TickType_t timeout);
