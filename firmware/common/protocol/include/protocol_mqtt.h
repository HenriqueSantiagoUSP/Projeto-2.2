#pragma once

/*
 * MQTT topics and enumerated values (docs/protocolo.md §1).
 *
 * Topics are idoso/<site>/<room>/<suffix>; the suffixes and every string value a
 * consumer may match on live here, so the firmware never spells them twice.
 */

#define MQTT_ROOT                 "idoso"
#define MQTT_JSON_VERSION         1

/* Topic suffixes, appended to idoso/<site>/<room>/. */
#define MQTT_TOPIC_STATUS         "status"
#define MQTT_TOPIC_DOOR           "porta"          /* + "/<door id>" */
#define MQTT_TOPIC_PRESENCE       "presenca"
#define MQTT_TOPIC_TEMPERATURE    "temperatura"
#define MQTT_TOPIC_WEARABLE       "pulseira"       /* + "/<wearable id>" */
#define MQTT_TOPIC_ALERT          "alerta"

/* Published by the wearable itself, only when its optional MQTT link is on:
   pulseira/<wearable id>/<suffix>. */
#define MQTT_TOPIC_WEARABLE_TELEMETRY  "telemetria"
#define MQTT_TOPIC_WEARABLE_LINK       "mqtt"     /* online, or offline as its Last Will */
#define MQTT_TOPIC_CONFIG_WANTED  "config/desejada"
#define MQTT_TOPIC_CONFIG_APPLIED "config/aplicada"

/*
 * Periodic readings are published on change, not on every reading (common/report):
 * the temperature when it moved by MQTT_TEMP_REPORT_DELTA_C from the value last sent,
 * and the wearable summaries every MQTT_REPORT_MAX_INTERVAL_S. A reading that does not
 * change still goes out at least every MQTT_REPORT_MAX_INTERVAL_S, as a sign of life.
 */
#define MQTT_REPORT_MAX_INTERVAL_S  300
#define MQTT_TEMP_REPORT_DELTA_C    0.2f

/* "estado" values. */
#define MQTT_STATE_ONLINE         "online"
#define MQTT_STATE_OFFLINE        "offline"
#define MQTT_STATE_OPEN           "aberta"
#define MQTT_STATE_CLOSED         "fechada"
#define MQTT_STATE_DETECTED       "detectada"
#define MQTT_STATE_CLEAR          "livre"

/* "tipo" values of an alert. */
#define MQTT_ALERT_FALL           "queda"
#define MQTT_ALERT_HELP           "ajuda"
#define MQTT_ALERT_NIGHT_EXIT     "saida_noturna"
#define MQTT_ALERT_BATHROOM       "banheiro_prolongado"
#define MQTT_ALERT_INACTIVITY     "inatividade"
#define MQTT_ALERT_TEMPERATURE    "temperatura"
#define MQTT_ALERT_WEARABLE_OFF   "pulseira_offline"
#define MQTT_ALERT_LOW_BATTERY    "bateria_baixa"

/* "estado" values of an alert episode. */
#define MQTT_ALERT_ACTIVE         "ativo"
#define MQTT_ALERT_RESOLVED       "resolvido"
#define MQTT_ALERT_CANCELLED      "cancelado"

/* Door roles in the configuration. */
#define MQTT_DOOR_ROLE_EXIT       "saida"
#define MQTT_DOOR_ROLE_BATHROOM   "banheiro"
#define MQTT_DOOR_ROLE_OTHER      "outra"
