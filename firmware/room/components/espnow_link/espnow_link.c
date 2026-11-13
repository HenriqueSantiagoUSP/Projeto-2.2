#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "espnow_link.h"
#include "rtos_err.h"
#include "task_config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_wifi.h"

static const char *TAG = "espnow_link";

#define ESPNOW_QUEUE_DEPTH    16
#define ESPNOW_FRAME_MAX      16    /* the largest message is 12 bytes */
#define ESPNOW_MAX_SENDERS    4

typedef struct {
    uint8_t mac[ESP_NOW_ETH_ALEN];
    uint8_t data[ESPNOW_FRAME_MAX];
    uint8_t len;
    int8_t  rssi;
} espnow_frame_t;

/* Last message per sender, to drop resends of a message already delivered. */
typedef struct {
    uint8_t  mac[ESP_NOW_ETH_ALEN];
    bool     used;
    uint16_t seq;
    uint32_t uptime_ms;
} espnow_sender_t;

static QueueHandle_t s_queue = NULL;
static espnow_rx_cb_t s_cb = NULL;
static void *s_cb_ctx = NULL;
static espnow_sender_t s_senders[ESPNOW_MAX_SENDERS];
static volatile uint32_t s_dropped_frames = 0;

static void wearable_id(const uint8_t *mac, char id[ESPNOW_WEARABLE_ID_LEN]){
    snprintf(id, ESPNOW_WEARABLE_ID_LEN, "%02x%02x%02x", mac[3], mac[4], mac[5]);
}

/*
 * A wrong length here is a key the two boards would read differently (past the end of the
 * string), or a MAC that pairs with nobody: refuse to build rather than fail silently on air.
 */
#ifdef CONFIG_HUB_ESPNOW_ENCRYPT
_Static_assert(sizeof(CONFIG_HUB_ESPNOW_PMK) - 1 == ESP_NOW_KEY_LEN,
               "menuconfig > Room hub: ESP-NOW > the PMK must have exactly 16 characters");
_Static_assert(sizeof(CONFIG_HUB_ESPNOW_LMK) - 1 == ESP_NOW_KEY_LEN,
               "menuconfig > Room hub: ESP-NOW > the LMK must have exactly 16 characters");
#endif
_Static_assert(sizeof(CONFIG_HUB_WEARABLE_MAC) == 1 || sizeof(CONFIG_HUB_WEARABLE_MAC) == 18,
               "menuconfig > Room hub: ESP-NOW > the wearable MAC must be empty or aa:bb:cc:dd:ee:ff");

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

/* The paired wearable: with one configured, frames from anyone else are dropped. */
static bool s_paired = false;
static uint8_t s_paired_mac[ESP_NOW_ETH_ALEN];

/* Runs in the Wi-Fi task: copy and get out. */
static void on_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len){
    if (len <= 0 || len > ESPNOW_FRAME_MAX) {
        s_dropped_frames++;
        return;
    }
    espnow_frame_t frame = {
        .len = (uint8_t) len,
        .rssi = (int8_t) info->rx_ctrl->rssi,
    };
    memcpy(frame.mac, info->src_addr, ESP_NOW_ETH_ALEN);
    memcpy(frame.data, data, (size_t) len);
    if (xQueueSend(s_queue, &frame, 0) != pdTRUE) {
        s_dropped_frames++;
    }
}

/* True when this exact message was already delivered (a resend after a lost ACK). */
static bool is_duplicate(const espnow_frame_t *frame, const espnow_header_t *hdr){
    espnow_sender_t *slot = NULL;
    for (int i = 0; i < ESPNOW_MAX_SENDERS; i++) {
        if (s_senders[i].used && memcmp(s_senders[i].mac, frame->mac, ESP_NOW_ETH_ALEN) == 0) {
            slot = &s_senders[i];
            break;
        }
        if (slot == NULL && !s_senders[i].used) {
            slot = &s_senders[i];
        }
    }
    if (slot == NULL) {
        slot = &s_senders[0];   /* more senders than expected: recycle */
    }

    /*
     * A resend repeats the whole frame, uptime included. Comparing the uptime too
     * means a wearable that rebooted and restarted its seq is not taken for a resend.
     */
    if (slot->used && memcmp(slot->mac, frame->mac, ESP_NOW_ETH_ALEN) == 0 &&
        slot->seq == hdr->seq && slot->uptime_ms == hdr->uptime_ms) {
        return true;
    }
    memcpy(slot->mac, frame->mac, ESP_NOW_ETH_ALEN);
    slot->used = true;
    slot->seq = hdr->seq;
    slot->uptime_ms = hdr->uptime_ms;
    return false;
}

static void espnow_task(void *arg){
    (void) arg;
    espnow_frame_t frame;
    uint32_t reported_drops = 0;

    while (1) {
        if (xQueueReceive(s_queue, &frame, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (s_dropped_frames != reported_drops) {
            ESP_LOGW(TAG, "%lu frame(s) dropped", (unsigned long) (s_dropped_frames - reported_drops));
            reported_drops = s_dropped_frames;
        }

        if (!espnow_msg_valid(frame.data, frame.len)) {
            ESP_LOGW(TAG, "malformed or unknown message from " MACSTR " (%u bytes)",
                     MAC2STR(frame.mac), frame.len);
            continue;
        }
        if (s_paired && memcmp(frame.mac, s_paired_mac, ESP_NOW_ETH_ALEN) != 0) {
            /* Not our wearable: it could raise a false fall or fill the tables. Said once. */
            static bool warned = false;
            if (!warned) {
                warned = true;
                ESP_LOGW(TAG, "frames from " MACSTR " ignored: only the paired wearable is accepted",
                         MAC2STR(frame.mac));
            }
            continue;
        }
        const espnow_header_t *hdr = (const espnow_header_t *) frame.data;
        if (is_duplicate(&frame, hdr)) {
            continue;
        }

        espnow_rx_msg_t msg = {
            .type = hdr->type,
            .seq = hdr->seq,
            .uptime_ms = hdr->uptime_ms,
            .rssi = frame.rssi,
        };
        wearable_id(frame.mac, msg.wearable);

        switch (hdr->type) {
            case ESPNOW_MSG_HEARTBEAT: {
                const espnow_heartbeat_t *hb = (const espnow_heartbeat_t *) frame.data;
                msg.heartbeat.battery_mv = hb->battery_mv;
                msg.heartbeat.activity_mg = hb->activity_mg;
                break;
            }
            case ESPNOW_MSG_FALL: {
                const espnow_fall_t *fall = (const espnow_fall_t *) frame.data;
                msg.fall.a2_peak = fall->a2_peak;
                msg.fall.prob_pct = fall->prob_pct;
                break;
            }
            case ESPNOW_MSG_FALL_CANCEL: {
                const espnow_fall_cancel_t *cancel = (const espnow_fall_cancel_t *) frame.data;
                msg.fall_cancel.fall_seq = cancel->fall_seq;
                break;
            }
            default:
                break;   /* PING: being heard is the whole message */
        }

        if (s_cb != NULL) {
            s_cb(&msg, s_cb_ctx);
        }
    }
}

bool espnow_link_paired_wearable(char id[ESPNOW_WEARABLE_ID_LEN]){
    uint8_t mac[ESP_NOW_ETH_ALEN];
    if (!parse_mac(CONFIG_HUB_WEARABLE_MAC, mac)) {
        return false;
    }
    wearable_id(mac, id);
    return true;
}

esp_err_t espnow_link_init(espnow_rx_cb_t cb, void *ctx){
    s_cb = cb;
    s_cb_ctx = ctx;

    s_queue = xQueueCreate(ESPNOW_QUEUE_DEPTH, sizeof(espnow_frame_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) return err;
    err = esp_now_register_recv_cb(on_recv);
    if (err != ESP_OK) return err;

    uint8_t mac[ESP_NOW_ETH_ALEN];
    bool paired = parse_mac(CONFIG_HUB_WEARABLE_MAC, mac);
    if (!paired && CONFIG_HUB_WEARABLE_MAC[0] != '\0') {
        /* Configured but unreadable: say it loudly, the wearable will not be heard as expected. */
        ESP_LOGE(TAG, "wearable MAC \"%s\" is not aa:bb:cc:dd:ee:ff (menuconfig > Room hub: ESP-NOW): no "
                 "wearable paired", CONFIG_HUB_WEARABLE_MAC);
    }
    s_paired = paired;
    memcpy(s_paired_mac, mac, ESP_NOW_ETH_ALEN);

#ifdef CONFIG_HUB_ESPNOW_ENCRYPT
    if (paired) {
        err = esp_now_set_pmk((const uint8_t *) CONFIG_HUB_ESPNOW_PMK);
        if (err != ESP_OK) return err;

        /* An encrypted frame is only accepted from a registered peer holding the same LMK. */
        esp_now_peer_info_t peer = {
            .ifidx = WIFI_IF_STA,
            .channel = 0,          /* whatever channel the station is on */
            .encrypt = true,
        };
        memcpy(peer.peer_addr, mac, ESP_NOW_ETH_ALEN);
        memcpy(peer.lmk, CONFIG_HUB_ESPNOW_LMK, ESP_NOW_KEY_LEN);
        err = esp_now_add_peer(&peer);
        if (err != ESP_OK) return err;
        ESP_LOGI(TAG, "paired with " MACSTR ", encrypted", MAC2STR(mac));
    } else {
        ESP_LOGW(TAG, "encryption on but no wearable MAC configured: accepting unencrypted frames");
    }
#else
    if (paired) {
        ESP_LOGW(TAG, "paired with " MACSTR ", NOT encrypted", MAC2STR(mac));
    }
#endif

    /* Core 0 with the radio that fills its queue. */
    return esp_err_from_pdpass(xTaskCreatePinnedToCore(espnow_task, "espnow_task", STACK_MEDIUM, NULL,
                                                       PRIO_MEDIA, NULL, CORE_PROTOCOL));
}
