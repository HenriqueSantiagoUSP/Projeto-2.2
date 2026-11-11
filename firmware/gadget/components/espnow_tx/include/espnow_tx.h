#pragma once

/*
 * ESP-NOW transmission from the wearable to its hub (docs/protocolo.md §3).
 *
 * The radio is started by net_init(). A message counts as delivered only on the
 * hub's MAC-level ACK. After ESPNOW_SCAN_AFTER_FAILURES failures in a row the
 * channel is scanned with PINGs: the hub follows the channel of its access point,
 * and a router may change it. When the wearable itself joins that access point
 * (fixed_channel) the channel already is the hub's, and a scan only PINGs on it.
 * The sequence number and the last good channel live in RTC memory, so they survive
 * a sleep reset (not a power loss).
 *
 * Not thread safe: one task (the wearable's comm task) owns every call.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "protocol_espnow.h"

typedef struct {
    const char *hub_mac;   /* "aa:bb:cc:dd:ee:ff" */
    bool        encrypt;
    const char *pmk;       /* 16 characters, same as the hub */
    const char *lmk;       /* 16 characters, same as the hub */
    bool        fixed_channel;   /* the station joined an access point: never change channel */
} espnow_tx_config_t;

/* Registers the hub as a peer. Needs net_init() first (it starts the radio). */
esp_err_t espnow_tx_init(const espnow_tx_config_t *config);

/* Fills a header for a new message: next seq, current uptime. */
void espnow_tx_header(espnow_header_t *hdr, espnow_msg_type_t type);

/* Up to `attempts` tries, ESPNOW_TX_RETRY_GAP_MS apart. ESP_OK only on the hub's ACK. */
esp_err_t espnow_tx_send(const void *msg, size_t len, int attempts);

/* Looks for the hub on every channel. True when found; it is then the current channel. */
bool espnow_tx_scan(void);

/* Channel in use, 0 before the hub was ever found. */
uint8_t espnow_tx_channel(void);
