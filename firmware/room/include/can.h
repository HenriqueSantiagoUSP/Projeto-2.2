#pragma once

/*
 * CAN (TWAI) transport.
 *
 * This header is about THIS node: which group it belongs to, which sensors it
 * publishes and under which nibbles, and how to send and receive frames. The
 * map of the whole bus, and how to read somebody else's id, lives in can_ids.h.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "can_ids.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_err.h"

/* Which group this node is. The G in the 0x0GS ids it sends. */
#define CAN_NODE_GROUP      CAN_GROUP_ROOM

/* Builds the 11-bit standard id for one of this node's own sensors. */
#define CAN_NODE_MSG_ID(sensor)  CAN_MSG_ID(CAN_NODE_GROUP, sensor)

#define CAN_BITRATE         250000
#define CAN_QUEUE_DEPTH     5
#define CAN_MAX_PAYLOAD     8       /* classic CAN frame */
#define CAN_TX_TIMEOUT_MS   10      /* max wait for a free slot in the TX queue */
#define CAN_TX_DRAIN_MS     50      /* max wait for the previous frame to leave the TX buffer */

/* Max number of RX handlers that can be registered. */
#define CAN_RX_HANDLER_MAX  8

/*
 * Bench mode: the node acknowledges its own frames (self test) and receives them
 * back (loopback), so it works with no other node on the bus. Set to false for a
 * real bus, where a peer provides the ACK.
 */
#define CAN_SELF_TEST       false

/*
 * Sensor nibble: the S in 0x0GS, for the sensors THIS node publishes. Must fit in
 * 4 bits. These values are the single place this node's nibbles are written; the
 * bus directory in can_ids.c refers back to them rather than repeating the hex.
 *
 * A sensor only belongs here once it actually transmits.
 */
typedef enum {
    CAN_SENSOR_HCSR501    = 0x1,
    CAN_SENSOR_REEDSWITCH = 0x3,
} can_sensor_id_t;

typedef struct {
    uint32_t id;
    uint8_t  data[CAN_MAX_PAYLOAD];
    uint8_t  length;
    uint32_t timestamp;
} can_rx_event_t;

/* Wildcards for can_rx_handler_register(): match every group, or every kind. */
#define CAN_GROUP_ANY   ((can_group_t) 0xFF)
#define CAN_KIND_ANY    ((can_sensor_kind_t) 0xFF)

/*
 * Called for each received frame whose sender matches the filter it was
 * registered with. Runs in the can_task context, never in an ISR, so it may log
 * and block briefly.
 *
 * info->sensor is NULL when the sender's group is known but that sensor nibble is
 * not in its table. Only a CAN_KIND_ANY handler is ever called with it NULL: a
 * handler filtering on a kind cannot match a sensor that could not be resolved.
 */
typedef void (*can_rx_handler_t)(const can_rx_event_t *event,
                                 const can_id_info_t *info,
                                 void *ctx);

esp_err_t can_init(void);

/*
 * Registers a handler for frames from a given group and sensor kind, either of
 * which may be CAN_GROUP_ANY / CAN_KIND_ANY.
 *
 * Must be called before can_init(), which is what starts the task that reads the
 * registry; there is no locking on it.
 */
esp_err_t can_rx_handler_register(can_group_t group, can_sensor_kind_t kind,
                                  can_rx_handler_t handler, void *ctx);

/* Queues one frame for transmission. Never blocks longer than CAN_TX_TIMEOUT_MS. */
esp_err_t can_transmit(can_sensor_id_t sensor, const uint8_t *data, size_t len);
