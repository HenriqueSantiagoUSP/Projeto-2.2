#pragma once

/*
 * ESP-NOW reception from the wearable (docs/protocolo.md §3).
 *
 * The Wi-Fi task only copies each frame into a queue; espnow_task validates it,
 * drops resends (same seq and same sender uptime) and hands it to the hub.
 * The hub never transmits: the delivery ACK is sent by the radio itself.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "protocol_espnow.h"

#define ESPNOW_WEARABLE_ID_LEN  7   /* last 3 MAC bytes in hex + NUL: "a1b2c3" */

typedef struct {
    char    wearable[ESPNOW_WEARABLE_ID_LEN];
    uint8_t type;        /* espnow_msg_type_t */
    uint16_t seq;
    uint32_t uptime_ms;  /* sender uptime: a resend repeats it, a restarted wearable does not */
    int8_t  rssi;        /* dBm, as the hub heard it */
    union {
        struct { uint16_t battery_mv; uint16_t activity_mg; } heartbeat;
        struct { float a2_peak; uint8_t prob_pct; } fall;
        struct { uint16_t fall_seq; } fall_cancel;
        struct { uint16_t help_seq; } help_cancel;   /* HELP itself has no payload */
    };
} espnow_rx_msg_t;

typedef void (*espnow_rx_cb_t)(const espnow_rx_msg_t *msg, void *ctx);

/* Needs the Wi-Fi started (net_init). The callback runs in espnow_task. */
esp_err_t espnow_link_init(espnow_rx_cb_t cb, void *ctx);

/* Id of the configured wearable (CONFIG_HUB_WEARABLE_MAC); false when none is configured. */
bool espnow_link_paired_wearable(char id[ESPNOW_WEARABLE_ID_LEN]);
