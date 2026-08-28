#pragma once

/*
 * CAN (TWAI) transport.
 *
 * Message IDs follow the 0x0GS layout of this network:
 *   G = group nibble  (which node is talking)
 *   S = sensor nibble (which sensor of that group)
 * so a standard 11-bit ID is built as (group << 4) | sensor.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_err.h"

/* Group nibble (0x0-0xF) of this node: the G in 0x0GS. */
#define CAN_NODE_GROUP      0xF

/* Builds the 11-bit standard ID for a sensor of this node. */
#define CAN_MSG_ID(sensor)  ((((CAN_NODE_GROUP) & 0xF) << 4) | ((sensor) & 0xF))

#define CAN_BITRATE         250000
#define CAN_QUEUE_DEPTH     5
#define CAN_MAX_PAYLOAD     8       /* classic CAN frame */
#define CAN_TX_TIMEOUT_MS   10      /* max wait for a free slot in the TX queue */
#define CAN_TX_DRAIN_MS     50      /* max wait for the previous frame to leave the TX buffer */

/*
 * Bench mode: the node acknowledges its own frames (self test) and receives them
 * back (loopback), so it works with no other node on the bus. Set to false for a
 * real bus, where a peer provides the ACK.
 */
#define CAN_SELF_TEST       false

/* Sensor nibble: the S in 0x0GS. Must fit in 4 bits. */
typedef enum {
    CAN_SENSOR_HCSR501    = 0x1,
    CAN_SENSOR_LM35       = 0x2,
    CAN_SENSOR_REEDSWITCH = 0x3,
} can_sensor_id_t;

typedef struct {
    uint32_t id;
    uint8_t  data[CAN_MAX_PAYLOAD];
    uint8_t  length;
    uint32_t timestamp;
} can_rx_event_t;

esp_err_t can_init(void);

/* Queues one frame for transmission. Never blocks longer than CAN_TX_TIMEOUT_MS. */
esp_err_t can_transmit(can_sensor_id_t sensor, const uint8_t *data, size_t len);
