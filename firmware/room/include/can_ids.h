#pragma once

/*
 * CAN ID map of the bus.
 *
 * Every message id on this network follows the 0x0GS layout:
 *   G = group nibble  (which node is talking)
 *   S = sensor nibble (which sensor of that group)
 *
 * The sensor nibble is chosen by each group independently, so the same sensor
 * model can be 0x1 in one group and 0x2 in another. Nothing about the sender is
 * carried in the payload: the 8 data bytes are all payload, and the identity
 * lives entirely in the id.
 *
 * This header is the directory of the whole bus, the counterpart of can.h:
 * can.h knows how to build this node's own ids, this one knows how to read
 * anybody's. It stays a leaf on purpose (no project includes) so that both the
 * transport and the directory table can depend on it without a cycle.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Group nibble: the G in 0x0GS. One entry per node on the bus. */
typedef enum {
    CAN_GROUP_ROOM = 0xF,
} can_group_t;

/*
 * What a sensor measures, and with it how to read the payload. This is the
 * contract between sender and receiver, so it describes the measurement rather
 * than the part number: a group swapping an HC-SR501 for another PIR still
 * reports presence, and receivers do not change. The part number is the name
 * string in can_sensor_desc_t.
 */
typedef enum {
    CAN_SENSOR_KIND_PRESENCE,      /* 1 byte:  0 = clear, 1 = presence          */
    CAN_SENSOR_KIND_CONTACT,       /* 1 byte:  0 = closed, 1 = open             */
    CAN_SENSOR_KIND_TEMPERATURE,   /* 2 bytes: int16 little endian, 1/100 deg C */
    CAN_SENSOR_KIND_COUNT,         /* sizes the payload table, keep last        */
} can_sensor_kind_t;

/* One sensor of one group. */
typedef struct {
    uint8_t            nibble;   /* the S in 0x0GS, chosen by the owning group */
    can_sensor_kind_t  kind;
    const char        *name;     /* the actual part, for logs: "hcsr501" */
} can_sensor_desc_t;

/* One node on the bus and everything it publishes. */
typedef struct {
    can_group_t               group;
    const char               *name;
    const can_sensor_desc_t  *sensors;
    uint8_t                   sensor_count;
} can_group_desc_t;

/* Result of can_id_decode(). Either field is NULL when it could not be resolved. */
typedef struct {
    const can_group_desc_t   *group;
    const can_sensor_desc_t  *sensor;
} can_id_info_t;

/* Builds the 11-bit standard id for any (group, sensor) pair of this network. */
#define CAN_MSG_ID(group, sensor)   ((((group) & 0xF) << 4) | ((sensor) & 0xF))

/* Splits an id back into its nibbles. */
#define CAN_ID_GROUP(id)            (((id) >> 4) & 0xF)
#define CAN_ID_SENSOR(id)           ((id) & 0xF)

/*
 * Resolves a received id against the directory.
 *
 * Returns false when the id is not a 0x0GS id of this network, or when its group
 * is unknown; *out is then zeroed. Returns true with out->sensor == NULL when the
 * group is known but that sensor nibble is not in its table, which is the normal
 * case for a group that added a sensor we have not registered yet, and is worth
 * telling apart from a foreign id.
 */
bool can_id_decode(uint32_t id, can_id_info_t *out);

/* Expected payload length of a kind, in bytes. 0 when variable or unknown. */
uint8_t can_kind_payload_len(can_sensor_kind_t kind);

/* The directory itself, for enumerating the bus (a boot log, for instance). */
const can_group_desc_t *can_ids_groups(size_t *count);
