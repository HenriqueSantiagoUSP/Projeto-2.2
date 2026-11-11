#include <stdlib.h>

#include "hub_config.h"
#include "nvs.h"

#define HUB_CONFIG_NAMESPACE   "hub"
#define HUB_CONFIG_KEY         "config"

esp_err_t hub_config_load(char **json){
    *json = NULL;

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(HUB_CONFIG_NAMESPACE, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;   /* never saved */
    }
    if (err != ESP_OK) return err;

    size_t len = 0;
    err = nvs_get_str(nvs, HUB_CONFIG_KEY, NULL, &len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs);
        return ESP_OK;
    }
    if (err == ESP_OK) {
        *json = malloc(len);
        err = (*json == NULL) ? ESP_ERR_NO_MEM : nvs_get_str(nvs, HUB_CONFIG_KEY, *json, &len);
        if (err != ESP_OK) {
            free(*json);
            *json = NULL;
        }
    }
    nvs_close(nvs);
    return err;
}

esp_err_t hub_config_save(const char *json){
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(HUB_CONFIG_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) return err;

    err = nvs_set_str(nvs, HUB_CONFIG_KEY, json);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}
