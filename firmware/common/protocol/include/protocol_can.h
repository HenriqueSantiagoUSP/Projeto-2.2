#pragma once

/*
 * CAN frames of this project (docs/protocolo.md §2).
 *
 * Every id is 0x0GS: G = group (which node), S = sensor nibble (what it measures).
 * The hub and the wearable are both group 0xF and split its sensor nibbles. All
 * payloads are little endian, and nothing about the sender goes in them: the
 * identity is entirely in the id.
 *
 * The wearable's frames exist for bench tests and the course requirement only: in
 * use it talks to the hub over ESP-NOW and has no transceiver.
 */

#include <stdint.h>

#define CAN_GROUP_PROJECT           0xF

/* Sensor nibbles of group 0xF. 0x0, 0xB, 0xC and 0xF are free. */
#define CAN_NIBBLE_PRESENCE         0x1   /* hub, HC-SR501: uint8, 0 clear / 1 presence      */
#define CAN_NIBBLE_TEMPERATURE      0x2   /* hub, LM35: int16, 1/100 degC                    */
#define CAN_NIBBLE_DOOR_FIRST       0x3   /* hub, door i = 0x3 + i: uint8, 0 closed / 1 open */
#define CAN_NIBBLE_DOOR_LAST        0xA
#define CAN_NIBBLE_FALL             0xD   /* wearable: int16 peak |a|^2 (1/100 g^2) + uint8 P(fall) % */
#define CAN_NIBBLE_WEARABLE         0xE   /* wearable: uint16 battery mV + uint16 activity mg */

#define CAN_DOORS_MAX               (CAN_NIBBLE_DOOR_LAST - CAN_NIBBLE_DOOR_FIRST + 1)
#define CAN_NIBBLE_DOOR(i)          (CAN_NIBBLE_DOOR_FIRST + (i))

/* Payload length of each frame, in bytes. */
#define CAN_LEN_PRESENCE            1
#define CAN_LEN_TEMPERATURE         2
#define CAN_LEN_DOOR                1
#define CAN_LEN_FALL                3
#define CAN_LEN_WEARABLE            4

static inline void can_put_u16_le(uint8_t *out, uint16_t value){
    out[0] = (uint8_t) (value & 0xFF);
    out[1] = (uint8_t) (value >> 8);
}

static inline uint16_t can_get_u16_le(const uint8_t *in){
    return (uint16_t) (in[0] | (in[1] << 8));
}

static inline void can_put_i16_le(uint8_t *out, int16_t value){
    can_put_u16_le(out, (uint16_t) value);
}

static inline int16_t can_get_i16_le(const uint8_t *in){
    return (int16_t) can_get_u16_le(in);
}
