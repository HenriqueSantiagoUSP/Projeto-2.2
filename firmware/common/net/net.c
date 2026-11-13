#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "net.h"
#include "sys_events.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"

static const char *TAG = "net";

#define NET_BACKOFF_MIN_MS   1000
#define NET_BACKOFF_MAX_MS   60000
#define NET_RETRY_AFTER_ERROR_MS 5000   /* connect() itself refused: no event will come, try again */

/* Set once by net_init(), then only read. */
static net_event_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;
static bool s_joins_ap = false;

/* Touched only by the event loop task. */
static uint32_t s_backoff_ms = NET_BACKOFF_MIN_MS;
static esp_timer_handle_t s_reconnect_timer = NULL;
static bool s_outage_reported = false;   /* one warning per outage; the retries stay quiet */

static bool ssid_configured(void){
    return CONFIG_APP_WIFI_SSID[0] != '\0';
}

/*
 * A refused connect() (the driver busy with a scan, say) raises no DISCONNECTED event, and the
 * retries hang on that event: without this the board would stay off the network until it
 * restarts. Retry on a timer instead; already connected, there is nothing to do.
 */
static void connect_or_retry(void){
    if (sys_events_all(SYS_EV_NET_CONNECTED)) {
        return;
    }
    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "connect refused (%s), trying again in %d s", esp_err_to_name(err), NET_RETRY_AFTER_ERROR_MS / 1000);
        esp_timer_start_once(s_reconnect_timer, (uint64_t) NET_RETRY_AFTER_ERROR_MS * 1000);
    }
}

/* Runs in the esp_timer task. */
static void reconnect_cb(void *arg){
    (void) arg;
    connect_or_retry();
}

/* Runs in the SNTP task. */
static void on_time_sync(struct timeval *tv){
    (void) tv;
    if (!sys_events_all(SYS_EV_TIME_VALID)) {
        ESP_LOGI(TAG, "clock set by SNTP");
    }
    sys_events_set(SYS_EV_TIME_VALID);
}

/* Why the access point was not reached, in words a person on the bench can act on. */
static const char *disconnect_reason(uint8_t reason){
    switch (reason) {
        case WIFI_REASON_NO_AP_FOUND:
        case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
            return "network not found";
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_AUTH_EXPIRE:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
            return "password refused?";
        case WIFI_REASON_BEACON_TIMEOUT:
            return "signal lost";
        default:
            return "could not connect";
    }
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data){
    (void) arg;

    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        if (s_joins_ap) {
            connect_or_retry();
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (!s_joins_ap) {
            return;
        }
        const wifi_event_sta_disconnected_t *info = (const wifi_event_sta_disconnected_t *) data;
        bool was_connected = sys_events_all(SYS_EV_NET_CONNECTED);
        sys_events_clear(SYS_EV_NET_CONNECTED);
        if (was_connected && s_cb != NULL) {
            s_cb(false, s_cb_ctx);
        }
        /* A missing network is a state, not an error: say it once, then retry quietly. */
        if (!s_outage_reported) {
            ESP_LOGW(TAG, "Wi-Fi \"%s\" not connected (%s, reason %u): carrying on without the network, "
                     "retrying every %d s at most", CONFIG_APP_WIFI_SSID, disconnect_reason(info->reason),
                     info->reason, NET_BACKOFF_MAX_MS / 1000);
            s_outage_reported = true;
        } else {
            ESP_LOGD(TAG, "still not connected (reason %u), retrying in %lu ms", info->reason,
                     (unsigned long) s_backoff_ms);
        }
        esp_timer_start_once(s_reconnect_timer, (uint64_t) s_backoff_ms * 1000);
        s_backoff_ms = s_backoff_ms * 2 > NET_BACKOFF_MAX_MS ? NET_BACKOFF_MAX_MS : s_backoff_ms * 2;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *got = (const ip_event_got_ip_t *) data;
        uint8_t channel = 0;
        wifi_second_chan_t second;
        esp_wifi_get_channel(&channel, &second);
        ESP_LOGI(TAG, "connected, ip " IPSTR ", channel %u", IP2STR(&got->ip_info.ip), channel);
        s_outage_reported = false;
        s_backoff_ms = NET_BACKOFF_MIN_MS;
        sys_events_set(SYS_EV_NET_CONNECTED);
        if (s_cb != NULL) {
            s_cb(true, s_cb_ctx);
        }
    }
}

esp_err_t net_init(const net_config_t *config){
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_cb = config->on_change;
    s_cb_ctx = config->ctx;
    s_joins_ap = config->join_ap && ssid_configured();

    setenv("TZ", CONFIG_APP_TIMEZONE, 1);
    tzset();

    esp_err_t err = esp_netif_init();
    if (err != ESP_OK) return err;
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;   /* already created is fine */
    esp_netif_create_default_wifi_sta();

    esp_timer_create_args_t timer_args = {
        .callback = reconnect_cb,
        .name = "wifi_reconnect",
    };
    err = esp_timer_create(&timer_args, &s_reconnect_timer);
    if (err != ESP_OK) return err;

    wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&init_config);
    if (err != ESP_OK) return err;
    /* The configuration comes from menuconfig on every boot: no need to wear the flash. */
    err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (err != ESP_OK) return err;

    err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    if (err != ESP_OK) return err;
    err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    if (err != ESP_OK) return err;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) return err;
    if (s_joins_ap) {
        wifi_config_t wifi_config = { 0 };
        strncpy((char *) wifi_config.sta.ssid, CONFIG_APP_WIFI_SSID, sizeof(wifi_config.sta.ssid));
        strncpy((char *) wifi_config.sta.password, CONFIG_APP_WIFI_PASSWORD, sizeof(wifi_config.sta.password));
        err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        if (err != ESP_OK) return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) return err;

    /* Modem sleep saves battery but dozes between beacons, missing ESP-NOW frames sent to us. */
    err = esp_wifi_set_ps(config->power_save ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
    if (err != ESP_OK) return err;

    if (!s_joins_ap) {
        if (config->join_ap) {
            /* Asked to join, but no network configured: bench mode on a fixed channel. */
            ESP_LOGW(TAG, "no SSID configured: ESP-NOW only, on channel %d", CONFIG_APP_WIFI_CHANNEL_NO_AP);
            return esp_wifi_set_channel(CONFIG_APP_WIFI_CHANNEL_NO_AP, WIFI_SECOND_CHAN_NONE);
        }
        ESP_LOGI(TAG, "radio only: the station does not join any network");
        return ESP_OK;
    }

    esp_sntp_config_t sntp_config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_APP_SNTP_SERVER);
    sntp_config.sync_cb = on_time_sync;
    return esp_netif_sntp_init(&sntp_config);
}

bool net_joins_ap(void){
    return s_joins_ap;
}

bool net_connected(void){
    return sys_events_all(SYS_EV_NET_CONNECTED);
}

bool net_time_valid(void){
    return sys_events_all(SYS_EV_TIME_VALID);
}
