#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "hub_config.h"
#include "protocol_mqtt.h"

#define FAIL(...)   do { snprintf(err, err_len, __VA_ARGS__); goto fail; } while (0)

/* "HH:MM" -> minutes after midnight; -1 when malformed. */
static int parse_hhmm(const char *text){
    unsigned h, m;
    char extra;
    if (text == NULL || sscanf(text, "%2u:%2u%c", &h, &m, &extra) != 2 || h > 23 || m > 59) {
        return -1;
    }
    return (int) (h * 60 + m);
}

/* Reads an optional integer key within [min, max]. Returns false (and sets err) only when present but invalid. */
static bool read_int(const cJSON *obj, const char *key, int min, int max, uint16_t *value,
                     char *err, size_t err_len){
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble != (double) (int) item->valuedouble ||
        item->valueint < min || item->valueint > max) {
        snprintf(err, err_len, "%s deve ser inteiro entre %d e %d", key, min, max);
        return false;
    }
    *value = (uint16_t) item->valueint;
    return true;
}

static bool read_float(const cJSON *obj, const char *key, float min, float max, float *value,
                       char *err, size_t err_len){
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (item == NULL) {
        return true;
    }
    if (!cJSON_IsNumber(item) || item->valuedouble < min || item->valuedouble > max) {
        snprintf(err, err_len, "%s deve estar entre %g e %g", key, (double) min, (double) max);
        return false;
    }
    *value = (float) item->valuedouble;
    return true;
}

static bool parse_role(const char *text, door_role_t *role){
    if (text == NULL) return false;
    if (strcmp(text, MQTT_DOOR_ROLE_EXIT) == 0)     { *role = DOOR_ROLE_EXIT;     return true; }
    if (strcmp(text, MQTT_DOOR_ROLE_BATHROOM) == 0) { *role = DOOR_ROLE_BATHROOM; return true; }
    if (strcmp(text, MQTT_DOOR_ROLE_OTHER) == 0)    { *role = DOOR_ROLE_OTHER;    return true; }
    return false;
}

static const char *role_name(door_role_t role){
    switch (role) {
        case DOOR_ROLE_EXIT:     return MQTT_DOOR_ROLE_EXIT;
        case DOOR_ROLE_BATHROOM: return MQTT_DOOR_ROLE_BATHROOM;
        case DOOR_ROLE_OTHER:    return MQTT_DOOR_ROLE_OTHER;
    }
    return MQTT_DOOR_ROLE_OTHER;
}

bool hub_config_from_json(const char *json, size_t len, const rules_params_t *base,
                          const char *const *door_ids, size_t door_count,
                          rules_params_t *out, char *err, size_t err_len){
    rules_params_t p = *base;
    cJSON *root = cJSON_ParseWithLength(json, len);

    if (!cJSON_IsObject(root)) {
        FAIL("JSON invalido");
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
    if (version != NULL && (!cJSON_IsNumber(version) || version->valueint != MQTT_JSON_VERSION)) {
        FAIL("versao %d nao suportada", cJSON_IsNumber(version) ? version->valueint : -1);
    }

    const cJSON *night = cJSON_GetObjectItemCaseSensitive(root, "noite");
    if (night != NULL) {
        if (!cJSON_IsObject(night)) FAIL("noite deve ser um objeto");
        const cJSON *start = cJSON_GetObjectItemCaseSensitive(night, "inicio");
        const cJSON *end = cJSON_GetObjectItemCaseSensitive(night, "fim");
        if (start != NULL) {
            int m = parse_hhmm(cJSON_GetStringValue(start));
            if (m < 0) FAIL("noite.inicio deve ser HH:MM");
            p.night_start_min = (uint16_t) m;
        }
        if (end != NULL) {
            int m = parse_hhmm(cJSON_GetStringValue(end));
            if (m < 0) FAIL("noite.fim deve ser HH:MM");
            p.night_end_min = (uint16_t) m;
        }
    }

    const cJSON *doors = cJSON_GetObjectItemCaseSensitive(root, "portas");
    if (doors != NULL) {
        if (!cJSON_IsObject(doors)) FAIL("portas deve ser um objeto");
        const cJSON *door;
        cJSON_ArrayForEach(door, doors) {
            size_t i = 0;
            while (i < door_count && strcmp(door_ids[i], door->string) != 0) i++;
            if (i == door_count) FAIL("porta desconhecida: %s", door->string);
            if (i >= RULES_MAX_DOORS) FAIL("porta fora do limite: %s", door->string);
            if (!parse_role(cJSON_GetStringValue(door), &p.door_role[i])) {
                FAIL("papel da porta %s deve ser saida, banheiro ou outra", door->string);
            }
        }
    }

    if (!read_int(root, "saida_noturna_min", 1, 720, &p.night_exit_min, err, err_len)) goto fail;
    if (!read_int(root, "banheiro_min", 1, 720, &p.bathroom_min, err, err_len)) goto fail;
    if (!read_int(root, "inatividade_min", 1, 1440, &p.inactivity_min, err, err_len)) goto fail;
    if (!read_int(root, "pulseira_offline_s", 30, 3600, &p.wearable_offline_s, err, err_len)) goto fail;
    if (!read_int(root, "bateria_baixa_mV", 3000, 4200, &p.low_battery_mv, err, err_len)) goto fail;

    const cJSON *temp = cJSON_GetObjectItemCaseSensitive(root, "temperatura");
    if (temp != NULL) {
        if (!cJSON_IsObject(temp)) FAIL("temperatura deve ser um objeto");
        if (!read_float(temp, "min", -10.0f, 50.0f, &p.temp_min_c, err, err_len)) goto fail;
        if (!read_float(temp, "max", -10.0f, 50.0f, &p.temp_max_c, err, err_len)) goto fail;
        if (!read_float(temp, "histerese", 0.0f, 5.0f, &p.temp_hysteresis_c, err, err_len)) goto fail;
        if (!read_int(temp, "persistencia_min", 0, 120, &p.temp_persist_min, err, err_len)) goto fail;
    }
    if (p.temp_max_c - p.temp_min_c <= 2.0f * p.temp_hysteresis_c) {
        FAIL("faixa de temperatura menor que duas vezes a histerese");
    }

    cJSON_Delete(root);
    *out = p;
    return true;

fail:
    cJSON_Delete(root);
    return false;
}

char *hub_config_to_json(const rules_params_t *params,
                         const char *const *door_ids, size_t door_count,
                         hub_config_origin_t origin, const char *error){
    static const char *const origins[] = {
        [HUB_CONFIG_DEFAULT] = "padrao",
        [HUB_CONFIG_NVS]     = "nvs",
        [HUB_CONFIG_MQTT]    = "mqtt",
    };
    char hhmm[12];   /* "HH:MM"; sized for any uint16_t so the compiler can prove it fits */
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) return NULL;

    cJSON_AddNumberToObject(root, "v", MQTT_JSON_VERSION);
    cJSON_AddStringToObject(root, "origem", origins[origin]);

    cJSON *night = cJSON_AddObjectToObject(root, "noite");
    snprintf(hhmm, sizeof(hhmm), "%02u:%02u", params->night_start_min / 60, params->night_start_min % 60);
    cJSON_AddStringToObject(night, "inicio", hhmm);
    snprintf(hhmm, sizeof(hhmm), "%02u:%02u", params->night_end_min / 60, params->night_end_min % 60);
    cJSON_AddStringToObject(night, "fim", hhmm);

    cJSON *doors = cJSON_AddObjectToObject(root, "portas");
    for (size_t i = 0; i < door_count && i < RULES_MAX_DOORS; i++) {
        cJSON_AddStringToObject(doors, door_ids[i], role_name(params->door_role[i]));
    }

    cJSON_AddNumberToObject(root, "saida_noturna_min", params->night_exit_min);
    cJSON_AddNumberToObject(root, "banheiro_min", params->bathroom_min);
    cJSON_AddNumberToObject(root, "inatividade_min", params->inactivity_min);

    cJSON *temp = cJSON_AddObjectToObject(root, "temperatura");
    cJSON_AddNumberToObject(temp, "min", params->temp_min_c);
    cJSON_AddNumberToObject(temp, "max", params->temp_max_c);
    cJSON_AddNumberToObject(temp, "histerese", params->temp_hysteresis_c);
    cJSON_AddNumberToObject(temp, "persistencia_min", params->temp_persist_min);

    cJSON_AddNumberToObject(root, "pulseira_offline_s", params->wearable_offline_s);
    cJSON_AddNumberToObject(root, "bateria_baixa_mV", params->low_battery_mv);

    if (error != NULL) {
        cJSON_AddStringToObject(root, "erro", error);
    }

    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return text;
}
