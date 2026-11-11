#pragma once

/*
 * Task priorities, cores and stack sizes of both boards (aulas/, slide "ESP32 - RTOS").
 *
 * Named levels instead of loose numbers, never above PRIO_CRITICA: the Wi-Fi task
 * runs at 23 and must keep preempting the application. Core 0 runs the radio and
 * everything that talks to it (Wi-Fi, ESP-NOW, MQTT, CAN, the hub's rules); core 1 is
 * left to real-time work (sampling, inputs), so a burst of network traffic never
 * delays a sample.
 */

#include "freertos/FreeRTOS.h"

#define PRIO_CRITICA    20   /* safety actions with a hard deadline (none yet) */
#define PRIO_ALTA       15   /* sensor reading with a hard deadline */
#define PRIO_MEDIA      10   /* processing, application logic, protocols */
#define PRIO_BAIXA      5    /* slow periodic readings */
#define PRIO_MINIMA     2    /* signalling, monitoring */

#define CORE_PROTOCOL   0    /* shared with the Wi-Fi stack */
#define CORE_REALTIME   1

/* Stack sizes in bytes (ESP-IDF counts bytes, not words). */
#define STACK_SMALL     3072
#define STACK_MEDIUM    4096
#define STACK_LARGE     6144

_Static_assert(PRIO_CRITICA < configMAX_PRIORITIES, "priority beyond the scheduler range");
