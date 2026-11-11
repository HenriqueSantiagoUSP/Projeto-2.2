#pragma once

/*
 * MQTT link to the central (docs/protocolo.md §1), shared by the hub and the wearable.
 *
 * Every topic is idoso/<site>/<room>/<suffix>; the board says which suffix carries
 * its Last Will and which one (if any) it listens to. Publishing never blocks, and
 * QoS 1 messages are kept in the outbox while the broker is unreachable. The
 * connection state is SYS_EV_MQTT_CONNECTED in sys_events.
 */

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

/* Runs in the MQTT task on every (re)connection, after the subscription is made. */
typedef void (*mqtt_link_connected_cb_t)(void *ctx);
/* A message on the subscribed topic. data is only valid during the call. */
typedef void (*mqtt_link_message_cb_t)(const char *data, size_t len, void *ctx);

typedef struct {
    const char              *will_suffix;       /* where the broker announces our absence */
    const char              *will_payload;      /* retained, QoS 1 */
    const char              *subscribe_suffix;  /* NULL: no subscription */
    mqtt_link_connected_cb_t on_connected;      /* optional */
    mqtt_link_message_cb_t   on_message;        /* optional */
    void                    *ctx;
} mqtt_link_config_t;

/* Creates and starts the client; it reconnects by itself. Needs net_init() and sys_events_init(). */
esp_err_t mqtt_link_start(const mqtt_link_config_t *config);

bool mqtt_link_connected(void);

/* Queues a message on idoso/<site>/<room>/<suffix>. Safe from any task. */
esp_err_t mqtt_link_publish(const char *suffix, const char *payload, int qos, bool retain);
