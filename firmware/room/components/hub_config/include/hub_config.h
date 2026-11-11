#pragma once

/*
 * Rule parameters of the hub as JSON (config/desejada in, config/aplicada out,
 * docs/protocolo.md §1) and their persistence in NVS.
 *
 * hub_config_json.c depends only on cJSON and rules.h, so it can be tested on the
 * host; hub_config_nvs.c holds the ESP-IDF part.
 */

#include <stdbool.h>
#include <stddef.h>

#include "rules.h"
#include "esp_err.h"

typedef enum {
    HUB_CONFIG_DEFAULT,   /* built-in values */
    HUB_CONFIG_NVS,       /* restored at boot */
    HUB_CONFIG_MQTT,      /* received on config/desejada */
} hub_config_origin_t;

/*
 * Parses and validates a configuration on top of base: keys left out keep their
 * base value. Returns false and writes the reason to err when anything is invalid;
 * out is then untouched, because a configuration is applied whole or not at all.
 */
bool hub_config_from_json(const char *json, size_t len, const rules_params_t *base,
                          const char *const *door_ids, size_t door_count,
                          rules_params_t *out, char *err, size_t err_len);

/*
 * The config/aplicada payload: the parameters in use, where they came from and,
 * when the last configuration was refused, why. NULL on allocation failure;
 * the caller frees the string.
 */
char *hub_config_to_json(const rules_params_t *params,
                         const char *const *door_ids, size_t door_count,
                         hub_config_origin_t origin, const char *error);

/* The last accepted configuration, as received. *json is NULL when none was saved; the caller frees it. */
esp_err_t hub_config_load(char **json);
esp_err_t hub_config_save(const char *json);
