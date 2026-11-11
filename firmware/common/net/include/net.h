#pragma once

/*
 * Wi-Fi station and wall clock, shared by the hub and the wearable.
 *
 * Two uses of the same radio:
 *  - join_ap: the board joins CONFIG_APP_WIFI_SSID, reconnecting with an exponential
 *    back-off from 1 s to 60 s, and sets its clock by SNTP (local time follows
 *    CONFIG_APP_TIMEZONE). The access point decides the channel, and ESP-NOW rides
 *    on it.
 *  - radio only: the station never associates; it only powers the radio for
 *    ESP-NOW, and the caller picks the channel.
 *
 * State goes to sys_events (SYS_EV_NET_CONNECTED, SYS_EV_TIME_VALID), so any task can
 * read it without sharing variables.
 */

#include <stdbool.h>

#include "esp_err.h"

typedef void (*net_event_cb_t)(bool connected, void *ctx);

typedef struct {
    bool           join_ap;      /* false: radio only, for ESP-NOW */
    bool           power_save;   /* false: never doze (mains powered; ESP-NOW reception) */
    net_event_cb_t on_change;    /* optional; runs in the event loop task */
    void          *ctx;
} net_config_t;

/* Needs nvs_flash_init() and sys_events_init() first. */
esp_err_t net_init(const net_config_t *config);

/* True when the board joins an access point (join_ap and an SSID configured). The AP
   then owns the channel: nobody else may change it. */
bool net_joins_ap(void);

bool net_connected(void);

/* True once SNTP has set the clock at least once. */
bool net_time_valid(void);
