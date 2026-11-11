#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "mqtt_link.h"
#include "mqtt_client.h"
#include "net.h"
#include "protocol_mqtt.h"
#include "sys_events.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"

static const char *TAG = "mqtt_link";

#define MQTT_TOPIC_MAX      96
#define MQTT_WILL_MAX       64
#define MQTT_RETRY_S        10      /* esp-mqtt's default reconnect_timeout_ms */

/* Written once by mqtt_link_start(), before the client exists; then only read. */
static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_link_connected_cb_t s_on_connected = NULL;
static mqtt_link_message_cb_t s_on_message = NULL;
static void *s_cb_ctx = NULL;
static bool s_disabled = false;          /* no network configured: never started */

/* Touched only by the MQTT task: one warning per outage, the retries stay quiet. */
static bool s_outage_reported = false;

static char s_will_topic[MQTT_TOPIC_MAX];
static char s_will_payload[MQTT_WILL_MAX];
static char s_subscribe_topic[MQTT_TOPIC_MAX];   /* "" when nothing is subscribed */

static void topic_for(char *out, size_t out_len, const char *suffix){
    snprintf(out, out_len, "%s/%s/%s/%s", MQTT_ROOT, CONFIG_APP_SITE, CONFIG_APP_ROOM, suffix);
}

static bool is_subscribed_topic(const esp_mqtt_event_handle_t event){
    size_t len = strlen(s_subscribe_topic);
    return len > 0 && event->topic_len == (int) len && strncmp(event->topic, s_subscribe_topic, len) == 0;
}

/* Runs in the MQTT client task. */
static void on_mqtt_event(void *arg, esp_event_base_t base, int32_t id, void *data){
    (void) arg;
    (void) base;
    esp_mqtt_event_handle_t event = data;

    switch ((esp_mqtt_event_id_t) id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "connected to %s", CONFIG_APP_MQTT_URI);
            s_outage_reported = false;
            sys_events_set(SYS_EV_MQTT_CONNECTED);
            if (s_subscribe_topic[0] != '\0') {
                esp_mqtt_client_subscribe_single(s_client, s_subscribe_topic, 1);
            }
            if (s_on_connected != NULL) {
                s_on_connected(s_cb_ctx);
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            if (mqtt_link_connected()) {
                ESP_LOGW(TAG, "broker connection lost: carrying on, retrying every %d s; QoS 1 messages wait "
                         "in the outbox", MQTT_RETRY_S);
                s_outage_reported = true;
            }
            sys_events_clear(SYS_EV_MQTT_CONNECTED);
            break;

        case MQTT_EVENT_DATA:
            if (!is_subscribed_topic(event)) {
                break;
            }
            /* Messages here are a few hundred bytes; one larger than the buffer arrives in pieces. */
            if (event->data_len != event->total_data_len) {
                ESP_LOGW(TAG, "message of %d bytes is too large, ignored", event->total_data_len);
                break;
            }
            if (s_on_message != NULL) {
                s_on_message(event->data, (size_t) event->data_len, s_cb_ctx);
            }
            break;

        case MQTT_EVENT_ERROR:
            /* The library's own logs are off (mqtt_link_start): one line per outage, from here. */
            if (s_outage_reported) {
                break;
            }
            s_outage_reported = true;
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                /* Not a missing piece but a wrong setting: the one error worth its level. */
                ESP_LOGE(TAG, "broker refused the connection (code %d): check user and password",
                         event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(TAG, "broker %s not reachable (errno %d): carrying on without it, retrying every %d s",
                         CONFIG_APP_MQTT_URI, event->error_handle->esp_transport_sock_errno, MQTT_RETRY_S);
            }
            break;

        default:
            break;
    }
}

/* Starts the client the first time the network comes up, whoever gets there first. */
static esp_event_handler_instance_t s_first_ip = NULL;
static portMUX_TYPE s_start_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_started = false;

static void start_once(void){
    taskENTER_CRITICAL(&s_start_lock);
    bool start = !s_started;
    s_started = true;
    taskEXIT_CRITICAL(&s_start_lock);
    if (start) {
        esp_err_t err = esp_mqtt_client_start(s_client);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "client not started: %s", esp_err_to_name(err));
        }
    }
}

/* Runs in the default event loop task. */
static void on_first_ip(void *arg, esp_event_base_t base, int32_t id, void *data){
    (void) arg;
    (void) base;
    (void) id;
    (void) data;
    start_once();
}

esp_err_t mqtt_link_start(const mqtt_link_config_t *config){
    if (config == NULL || config->will_suffix == NULL || config->will_payload == NULL ||
        strlen(config->will_payload) >= sizeof(s_will_payload)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    s_on_connected = config->on_connected;
    s_on_message = config->on_message;
    s_cb_ctx = config->ctx;

    topic_for(s_will_topic, sizeof(s_will_topic), config->will_suffix);
    snprintf(s_will_payload, sizeof(s_will_payload), "%s", config->will_payload);
    if (config->subscribe_suffix != NULL) {
        topic_for(s_subscribe_topic, sizeof(s_subscribe_topic), config->subscribe_suffix);
    } else {
        s_subscribe_topic[0] = '\0';
    }

    esp_mqtt_client_config_t client_config = {
        .broker.address.uri = CONFIG_APP_MQTT_URI,
        .credentials.username = CONFIG_APP_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_APP_MQTT_PASSWORD,
        .session = {
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
            /* The broker declares the board offline after 1.5 keepalives without traffic. */
            .keepalive = 30,
            .last_will = {
                .topic = s_will_topic,
                .msg = s_will_payload,
                .qos = 1,
                .retain = 1,
            },
        },
        .outbox.limit = (uint64_t) CONFIG_APP_MQTT_OUTBOX_KB * 1024,
    };

    if (!net_joins_ap()) {
        /* Nothing to reach: no client, no retries, no outbox filling up. */
        s_disabled = true;
        ESP_LOGW(TAG, "MQTT off: no Wi-Fi network configured (menu \"Network (Wi-Fi)\")");
        return ESP_OK;
    }

    /*
     * The library logs every failed attempt as an error; a broker that is not there is a
     * state, reported once per outage by on_mqtt_event instead.
     */
    esp_log_level_set("mqtt_client", ESP_LOG_NONE);
    esp_log_level_set("transport_base", ESP_LOG_NONE);
    esp_log_level_set("transport", ESP_LOG_NONE);
    esp_log_level_set("esp-tls", ESP_LOG_NONE);

    s_client = esp_mqtt_client_init(&client_config);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    esp_err_t err = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, on_mqtt_event, NULL);
    if (err != ESP_OK) return err;

    /*
     * First attempt once the network is up; after that the client reconnects by itself.
     * Registered before looking, so an address that arrives in between is not missed.
     */
    err = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_first_ip, NULL, &s_first_ip);
    if (err != ESP_OK) return err;
    if (net_connected()) {
        start_once();
    }
    return ESP_OK;
}

bool mqtt_link_connected(void){
    return sys_events_all(SYS_EV_MQTT_CONNECTED);
}

esp_err_t mqtt_link_publish(const char *suffix, const char *payload, int qos, bool retain){
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    char topic[MQTT_TOPIC_MAX];
    topic_for(topic, sizeof(topic), suffix);

    /*
     * enqueue never blocks on the network, and the client serializes it with its own
     * lock, so any task may call it. With store = true even a message queued while
     * disconnected is kept, and sent on reconnection.
     */
    int msg_id = esp_mqtt_client_enqueue(s_client, topic, payload, 0, qos, retain, true);
    if (msg_id < 0) {
        ESP_LOGW(TAG, "%s not queued (%s)", topic, msg_id == -2 ? "outbox full" : "error");
        return ESP_FAIL;
    }
    return ESP_OK;
}
