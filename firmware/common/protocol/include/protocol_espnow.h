#pragma once

/*
 * ESP-NOW messages from the wearable to the room hub (docs/protocolo.md §3).
 *
 * Both firmwares compile this same header, so the layout cannot drift between
 * sender and receiver. Every message is a packed little endian struct that starts
 * with espnow_header_t; the type field says which payload follows. Change the
 * layout only together with docs/protocolo.md, and bump ESPNOW_MSG_VERSION when an
 * old receiver could misread the new layout.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ESPNOW_MSG_VERSION          1

/* Timing contract between the two sides. */
#define ESPNOW_HEARTBEAT_PERIOD_MS  30000
#define ESPNOW_OFFLINE_TIMEOUT_MS   90000   /* three missed heartbeats */

/* Delivery policy for FALL, HELP and their cancels; PING and HEARTBEAT are never resent. */
#define ESPNOW_TX_RETRIES           10
#define ESPNOW_TX_RETRY_GAP_MS      50
#define ESPNOW_SCAN_AFTER_FAILURES  3
#define ESPNOW_CHANNEL_MIN          1
#define ESPNOW_CHANNEL_MAX          13

typedef enum {
    ESPNOW_MSG_PING        = 0x01,
    ESPNOW_MSG_HEARTBEAT   = 0x02,
    ESPNOW_MSG_FALL        = 0x03,
    ESPNOW_MSG_FALL_CANCEL = 0x04,
    ESPNOW_MSG_CONFIG      = 0x05,   /* reserved: hub -> wearable, not implemented yet */
    ESPNOW_MSG_HELP        = 0x06,   /* the resident held the button: "I need help" */
    ESPNOW_MSG_HELP_CANCEL = 0x07,
} espnow_msg_type_t;

typedef struct __attribute__((packed)) {
    uint8_t  version;     /* ESPNOW_MSG_VERSION */
    uint8_t  type;        /* espnow_msg_type_t */
    uint16_t seq;         /* +1 per new message; a resend repeats it */
    uint32_t uptime_ms;   /* sender uptime; with seq, tells a resend from a new message after a reboot */
} espnow_header_t;

typedef struct __attribute__((packed)) {
    espnow_header_t hdr;
} espnow_ping_t;

/* battery_mv when the wearable has no battery sense: the hub then skips the battery rule. */
#define ESPNOW_BATTERY_UNKNOWN      0xFFFF

typedef struct __attribute__((packed)) {
    espnow_header_t hdr;
    uint16_t battery_mv;   /* or ESPNOW_BATTERY_UNKNOWN */
    uint16_t activity_mg;  /* RMS of (|a| - 1 g) since the previous heartbeat */
} espnow_heartbeat_t;

typedef struct __attribute__((packed)) {
    espnow_header_t hdr;
    float   a2_peak;       /* filtered |a|^2 at the trigger peak, in g^2 */
    uint8_t prob_pct;      /* the classifier's P(fall), 0-100 % (0 = simulated by the button) */
} espnow_fall_t;

typedef struct __attribute__((packed)) {
    espnow_header_t hdr;
    uint16_t fall_seq;     /* seq of the FALL being cancelled */
} espnow_fall_cancel_t;

/* The header says it all: who asks for help, and the seq that names the request. */
typedef struct __attribute__((packed)) {
    espnow_header_t hdr;
} espnow_help_t;

typedef struct __attribute__((packed)) {
    espnow_header_t hdr;
    uint16_t help_seq;     /* seq of the HELP being cancelled */
} espnow_help_cancel_t;

_Static_assert(sizeof(espnow_header_t) == 8, "header layout is part of the contract");
_Static_assert(sizeof(espnow_heartbeat_t) == 12, "heartbeat layout is part of the contract");
_Static_assert(sizeof(espnow_fall_t) == 13, "fall layout is part of the contract");
_Static_assert(sizeof(espnow_fall_cancel_t) == 10, "fall cancel layout is part of the contract");
_Static_assert(sizeof(espnow_help_t) == 8, "help layout is part of the contract");
_Static_assert(sizeof(espnow_help_cancel_t) == 10, "help cancel layout is part of the contract");

/* Exact length a message of this type must have, 0 when the type is not known. */
static inline size_t espnow_msg_len(uint8_t type){
    switch (type) {
        case ESPNOW_MSG_PING:        return sizeof(espnow_ping_t);
        case ESPNOW_MSG_HEARTBEAT:   return sizeof(espnow_heartbeat_t);
        case ESPNOW_MSG_FALL:        return sizeof(espnow_fall_t);
        case ESPNOW_MSG_FALL_CANCEL: return sizeof(espnow_fall_cancel_t);
        case ESPNOW_MSG_HELP:        return sizeof(espnow_help_t);
        case ESPNOW_MSG_HELP_CANCEL: return sizeof(espnow_help_cancel_t);
        default:                     return 0;
    }
}

/* True when a received buffer is a well formed message of a version we understand. */
static inline bool espnow_msg_valid(const uint8_t *data, size_t len){
    if (data == NULL || len < sizeof(espnow_header_t)) {
        return false;
    }
    const espnow_header_t *hdr = (const espnow_header_t *) data;
    if (hdr->version != ESPNOW_MSG_VERSION) {
        return false;
    }
    size_t expected = espnow_msg_len(hdr->type);
    return expected != 0 && expected == len;
}
