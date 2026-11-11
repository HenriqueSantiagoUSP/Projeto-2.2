#include <string.h>

#include "can_ids.h"

#define ARRAY_LEN(a)   (sizeof(a) / sizeof((a)[0]))

/*
 * ---------------------------------------------------------------------------
 * The bus directory.
 *
 * One block per group. A group owns its own block and nothing else, so two
 * groups adding themselves touch different lines and do not conflict.
 *
 * To add a group, three edits:
 *   1. a value in can_group_t (include/can_ids.h) with its G nibble,
 *   2. a sensor array here,
 *   3. a row in s_groups.
 *
 * Example, for a group that picked 0x2 and publishes an LM35 as its sensor 0x1:
 *
 *   static const can_sensor_desc_t kitchen_sensors[] = {
 *       { 0x1, CAN_SENSOR_KIND_TEMPERATURE, "lm35" },
 *   };
 *
 * and in s_groups:
 *
 *   { CAN_GROUP_KITCHEN, "kitchen", kitchen_sensors, ARRAY_LEN(kitchen_sensors) },
 *
 * Our own block spells its nibbles with the protocol_can.h constants instead of
 * hex literals, so each nibble of the contract is written in exactly one place.
 * It lists every nibble the contract reserves, whichever board sends it: the hub
 * and the wearable read each other's frames on the bench.
 * ---------------------------------------------------------------------------
 */

#define PROJECT_DOOR(i)   { CAN_NIBBLE_DOOR(i), CAN_SENSOR_KIND_CONTACT, "door" #i }

static const can_sensor_desc_t project_sensors[] = {
    { CAN_NIBBLE_PRESENCE,    CAN_SENSOR_KIND_PRESENCE,    "hcsr501"  },
    { CAN_NIBBLE_TEMPERATURE, CAN_SENSOR_KIND_TEMPERATURE, "lm35"     },
    PROJECT_DOOR(0), PROJECT_DOOR(1), PROJECT_DOOR(2), PROJECT_DOOR(3),
    PROJECT_DOOR(4), PROJECT_DOOR(5), PROJECT_DOOR(6), PROJECT_DOOR(7),
    { CAN_NIBBLE_FALL,        CAN_SENSOR_KIND_FALL,        "fall"     },
    { CAN_NIBBLE_WEARABLE,    CAN_SENSOR_KIND_WEARABLE,    "wearable" },
};

_Static_assert(CAN_DOORS_MAX == 8, "project_sensors lists 8 doors: update it with the contract");

static const can_group_desc_t s_groups[] = {
    { CAN_GROUP_1, "group_1", project_sensors, ARRAY_LEN(project_sensors) },
};

/* Payload length per kind, indexed by can_sensor_kind_t. */
static const uint8_t k_payload_len[CAN_SENSOR_KIND_COUNT] = {
    [CAN_SENSOR_KIND_PRESENCE]    = CAN_LEN_PRESENCE,
    [CAN_SENSOR_KIND_CONTACT]     = CAN_LEN_DOOR,
    [CAN_SENSOR_KIND_TEMPERATURE] = CAN_LEN_TEMPERATURE,
    [CAN_SENSOR_KIND_FALL]        = CAN_LEN_FALL,
    [CAN_SENSOR_KIND_WEARABLE]    = CAN_LEN_WEARABLE,
};

static const can_group_desc_t *find_group(uint8_t nibble){
    for (size_t i = 0; i < ARRAY_LEN(s_groups); i++) {
        if ((uint8_t) s_groups[i].group == nibble) {
            return &s_groups[i];
        }
    }
    return NULL;
}

static const can_sensor_desc_t *find_sensor(const can_group_desc_t *group, uint8_t nibble){
    for (uint8_t i = 0; i < group->sensor_count; i++) {
        if (group->sensors[i].nibble == nibble) {
            return &group->sensors[i];
        }
    }
    return NULL;
}

bool can_id_decode(uint32_t id, can_id_info_t *out){
    if (out == NULL) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    /* 0x0GS only uses the low byte; anything above it is not ours. */
    if (id > 0xFF) {
        return false;
    }

    const can_group_desc_t *group = find_group(CAN_ID_GROUP(id));
    if (group == NULL) {
        return false;
    }

    out->group = group;
    out->sensor = find_sensor(group, CAN_ID_SENSOR(id));
    return true;
}

uint8_t can_kind_payload_len(can_sensor_kind_t kind){
    if ((unsigned) kind >= CAN_SENSOR_KIND_COUNT) {
        return 0;
    }
    return k_payload_len[kind];
}

const can_group_desc_t *can_ids_groups(size_t *count){
    if (count != NULL) {
        *count = ARRAY_LEN(s_groups);
    }
    return s_groups;
}
