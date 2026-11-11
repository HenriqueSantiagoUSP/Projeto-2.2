#pragma once

/*
 * CAN (TWAI) transport, shared by the hub and the wearable (aulas/, slide "ESP32 - CAN").
 *
 * Any task may call can_transmit(): it only copies the frame into a queue. The
 * can_tx task owns the peripheral: it hands the frames to the driver one at a time
 * and follows the node's error state, recovering from bus-off by itself. Received
 * frames go from the driver's ISR into a queue; the can_rx task resolves their ids
 * against the bus directory (can_ids.h) and calls the registered handlers.
 *
 * The node's state is in sys_events: SYS_EV_CAN_RUNNING, SYS_EV_CAN_BUS_OFF.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "can_ids.h"
#include "hal/gpio_types.h"
#include "esp_err.h"

#define CAN_BITRATE         250000
#define CAN_MAX_PAYLOAD     8       /* classic CAN frame */

/* Max number of RX handlers that can be registered. */
#define CAN_RX_HANDLER_MAX  8

typedef struct {
    gpio_num_t tx_gpio;
    gpio_num_t rx_gpio;
    uint8_t    group;       /* the G nibble of every id this node sends (protocol_can.h) */
} can_config_t;

typedef struct {
    uint32_t id;
    uint8_t  data[CAN_MAX_PAYLOAD];
    uint8_t  length;
    uint32_t timestamp;     /* RTOS ticks at reception */
} can_rx_event_t;

/* Wildcards for can_rx_handler_register(): match every group, or every kind. */
#define CAN_GROUP_ANY   ((can_group_t) 0xFF)
#define CAN_KIND_ANY    ((can_sensor_kind_t) 0xFF)

/*
 * Called for each received frame whose sender matches the filter it was
 * registered with. Runs in the can_rx task, never in an ISR, so it may log
 * and block briefly.
 *
 * info->sensor is NULL when the sender's group is known but that sensor nibble is
 * not in its table. Only a CAN_KIND_ANY handler is ever called with it NULL: a
 * handler filtering on a kind cannot match a sensor that could not be resolved.
 */
typedef void (*can_rx_handler_t)(const can_rx_event_t *event,
                                 const can_id_info_t *info,
                                 void *ctx);

/*
 * Registers a handler for frames from a given group and sensor kind, either of
 * which may be CAN_GROUP_ANY / CAN_KIND_ANY.
 *
 * Must be called before can_init(), which starts the task that reads the
 * registry; there is no locking on it.
 */
esp_err_t can_rx_handler_register(can_group_t group, can_sensor_kind_t kind,
                                  can_rx_handler_t handler, void *ctx);

/* Creates the node (bench mode from CONFIG_APP_CAN_SELF_TEST) and its two tasks. Needs sys_events_init(). */
esp_err_t can_init(const can_config_t *config);

/*
 * Queues one frame with id 0x0GS (G from the config, S = sensor). Never blocks:
 * ESP_ERR_TIMEOUT when the queue is full, ESP_ERR_INVALID_STATE while the node is
 * not running (not started, or bus-off), so stale frames are not kept for later.
 */
esp_err_t can_transmit(uint8_t sensor, const uint8_t *data, size_t len);

bool can_running(void);
