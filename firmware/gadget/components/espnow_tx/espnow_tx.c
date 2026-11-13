#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "espnow_tx.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"

static const char *TAG = "espnow_tx";

/* The ACK comes back within a few ms; a lost frame shows up as a FAIL status, not a timeout. */
#define ESPNOW_ACK_TIMEOUT_MS   100

static uint8_t s_hub[ESP_NOW_ETH_ALEN];
static SemaphoreHandle_t s_sent = NULL;
static volatile esp_now_send_status_t s_last_status;
static int s_failures = 0;
static bool s_scanning = false;
static bool s_fixed_channel = false;
/* A hub out of reach is a state: warned once when it goes, one line when it is back. comm_task only. */
static bool s_hub_missing = false;

/* Survive a reset from sleep, so the hub never sees a reused seq for a new message. */
static RTC_DATA_ATTR uint16_t s_seq;
static RTC_DATA_ATTR uint8_t s_channel;

/*
 * Sends whose completion never came in time. Callbacks arrive in send order, so the late
 * completion of a timed-out send shows up before the next send's own: skip that many, or a
 * late SUCCESS would be taken as the ACK of a FALL the hub never got.
 */
static portMUX_TYPE s_stale_lock = portMUX_INITIALIZER_UNLOCKED;
static int s_stale_completions = 0;

/* Runs in the Wi-Fi task: the status is written before the give, read after the take. */
static void on_sent(const esp_now_send_info_t *info, esp_now_send_status_t status){
    (void) info;
    taskENTER_CRITICAL(&s_stale_lock);
    bool stale = s_stale_completions > 0;
    if (stale) {
        s_stale_completions--;
    }
    taskEXIT_CRITICAL(&s_stale_lock);
    if (stale) {
        return;
    }
    s_last_status = status;
    xSemaphoreGive(s_sent);
}

/* Exactly "aa:bb:cc:dd:ee:ff": two hex digits per byte and nothing after. */
static bool parse_mac(const char *text, uint8_t mac[ESP_NOW_ETH_ALEN]){
    unsigned b[ESP_NOW_ETH_ALEN];
    int used = 0;
    if (text == NULL ||
        sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x%n", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &used)
            != ESP_NOW_ETH_ALEN ||
        used != 17 || text[used] != '\0') {
        return false;
    }
    for (int i = 0; i < ESP_NOW_ETH_ALEN; i++) {
        mac[i] = (uint8_t) b[i];
    }
    return true;
}

static int stale_pending(void){
    taskENTER_CRITICAL(&s_stale_lock);
    int n = s_stale_completions;
    taskEXIT_CRITICAL(&s_stale_lock);
    return n;
}

static esp_err_t send_once(const void *msg, size_t len){
    /*
     * Give a late completion one more ACK time to arrive and be dropped. If it never comes, forget
     * it: otherwise it would eat this send's real completion, and every send after it would fail.
     */
    for (int i = 0; i < 10 && stale_pending() > 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(ESPNOW_ACK_TIMEOUT_MS / 10));
    }
    taskENTER_CRITICAL(&s_stale_lock);
    s_stale_completions = 0;
    taskEXIT_CRITICAL(&s_stale_lock);
    xSemaphoreTake(s_sent, 0);   /* drop a completion left over from a timed out send */
    esp_err_t err = esp_now_send(s_hub, msg, len);
    if (err != ESP_OK) {
        return err;
    }
    if (xSemaphoreTake(s_sent, pdMS_TO_TICKS(ESPNOW_ACK_TIMEOUT_MS)) != pdTRUE) {
        /* Its completion may still come: on_sent drops it instead of crediting the next send. */
        taskENTER_CRITICAL(&s_stale_lock);
        s_stale_completions++;
        taskEXIT_CRITICAL(&s_stale_lock);
        return ESP_ERR_TIMEOUT;
    }
    return s_last_status == ESP_NOW_SEND_SUCCESS ? ESP_OK : ESP_FAIL;
}

void espnow_tx_header(espnow_header_t *hdr, espnow_msg_type_t type){
    hdr->version = ESPNOW_MSG_VERSION;
    hdr->type = (uint8_t) type;
    hdr->seq = ++s_seq;
    hdr->uptime_ms = (uint32_t) (esp_timer_get_time() / 1000);
}

/* Joined to an access point: the channel is the AP's (and the hub's); only check the hub hears us. */
static bool ping_current_channel(const espnow_ping_t *ping){
    bool found = send_once(ping, sizeof(*ping)) == ESP_OK;
    uint8_t channel = 0;
    wifi_second_chan_t second;
    if (esp_wifi_get_channel(&channel, &second) == ESP_OK) {
        s_channel = channel;
    }
    if (found) {
        ESP_LOGI(TAG, "hub answers on channel %u (the access point's)", s_channel);
        s_failures = 0;
        s_hub_missing = false;
    } else if (!s_hub_missing) {
        ESP_LOGW(TAG, "hub not heard on channel %u (the access point's): carrying on, falls go to the "
                 "fallbacks until it answers", s_channel);
        s_hub_missing = true;
    }
    return found;
}

bool espnow_tx_scan(void){
    espnow_ping_t ping;
    espnow_tx_header(&ping.hdr, ESPNOW_MSG_PING);
    if (s_fixed_channel) {
        return ping_current_channel(&ping);
    }

    s_scanning = true;

    /* Start from the last good channel: after a short outage the hub is usually still there. */
    uint8_t first = s_channel ? s_channel : ESPNOW_CHANNEL_MIN;
    int channels = ESPNOW_CHANNEL_MAX - ESPNOW_CHANNEL_MIN + 1;
    bool found = false;

    for (int k = 0; k < channels && !found; k++) {
        uint8_t ch = (uint8_t) (ESPNOW_CHANNEL_MIN + (first - ESPNOW_CHANNEL_MIN + k) % channels);
        if (esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE) != ESP_OK) {
            continue;
        }
        if (send_once(&ping, sizeof(ping)) == ESP_OK) {
            s_channel = ch;
            found = true;
        }
    }

    if (found) {
        ESP_LOGI(TAG, "hub found on channel %u", s_channel);
        s_failures = 0;
        s_hub_missing = false;
    } else {
        if (!s_hub_missing) {
            ESP_LOGW(TAG, "hub not found on any channel: carrying on, falls go to the fallbacks until it "
                     "answers");
            s_hub_missing = true;
        } else {
            ESP_LOGD(TAG, "hub still not found");
        }
        if (s_channel) {
            esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
        }
    }
    s_scanning = false;
    return found;
}

esp_err_t espnow_tx_send(const void *msg, size_t len, int attempts){
    for (int i = 0; i < attempts; i++) {
        if (send_once(msg, len) == ESP_OK) {
            s_failures = 0;
            return ESP_OK;
        }
        s_failures++;
        /* On a fixed channel there is nowhere else to look: just keep retrying. */
        if (!s_fixed_channel && s_failures >= ESPNOW_SCAN_AFTER_FAILURES && !s_scanning) {
            espnow_tx_scan();
        }
        if (i + 1 < attempts) {
            vTaskDelay(pdMS_TO_TICKS(ESPNOW_TX_RETRY_GAP_MS));
        }
    }
    return ESP_FAIL;
}

uint8_t espnow_tx_channel(void){
    return s_channel;
}

esp_err_t espnow_tx_init(const espnow_tx_config_t *config){
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!parse_mac(config->hub_mac, s_hub)) {
        ESP_LOGE(TAG, "hub MAC \"%s\" is not aa:bb:cc:dd:ee:ff", config->hub_mac ? config->hub_mac : "");
        return ESP_ERR_INVALID_ARG;
    }
    /* A shorter key would be read past its end: different bytes on each board, frames nobody can decrypt. */
    if (config->encrypt && (config->pmk == NULL || strlen(config->pmk) != ESP_NOW_KEY_LEN ||
                            config->lmk == NULL || strlen(config->lmk) != ESP_NOW_KEY_LEN)) {
        ESP_LOGE(TAG, "the PMK and the LMK must have exactly %d characters", ESP_NOW_KEY_LEN);
        return ESP_ERR_INVALID_ARG;
    }

    s_sent = xSemaphoreCreateBinary();
    if (s_sent == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /*
     * RTC memory only survives a deep-sleep wake-up: after a panic, the watchdog or a power cut the
     * seq would start at 0 again, repeating the seq (and the alert id) of a fall that may still be
     * open on the hub and at the central. Start from a random value instead (the radio is on, so
     * esp_random is a true random number).
     */
    if (esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        s_seq = (uint16_t) esp_random();
    }

    s_fixed_channel = config->fixed_channel;
    if (!s_fixed_channel && s_channel) {
        /* Back to the last good channel after a sleep reset; the scan corrects it if it moved. */
        esp_wifi_set_channel(s_channel, WIFI_SECOND_CHAN_NONE);
    }

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) return err;
    err = esp_now_register_send_cb(on_sent);
    if (err != ESP_OK) return err;

    esp_now_peer_info_t peer = {
        .ifidx = WIFI_IF_STA,
        .channel = 0,             /* follow the current channel, which the scan sets */
        .encrypt = config->encrypt,
    };
    memcpy(peer.peer_addr, s_hub, ESP_NOW_ETH_ALEN);
    if (config->encrypt) {
        err = esp_now_set_pmk((const uint8_t *) config->pmk);
        if (err != ESP_OK) return err;
        memcpy(peer.lmk, config->lmk, ESP_NOW_KEY_LEN);
    }
    err = esp_now_add_peer(&peer);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "hub " MACSTR "%s", MAC2STR(s_hub), config->encrypt ? ", encrypted" : ", NOT encrypted");
    }
    return err;
}
