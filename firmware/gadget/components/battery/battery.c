#include "battery.h"
#include "protocol_espnow.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "battery";

#define BATTERY_SAMPLES     8

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static adc_channel_t s_channel;
static uint32_t s_divider_x1000 = 0;

esp_err_t battery_init(gpio_num_t pin, uint32_t divider_x1000){
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(pin, &unit, &s_channel);
    if (err != ESP_OK) return err;
    if (unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d is on ADC2, which does not work with the radio on", pin);
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t unit_config = { .unit_id = unit };
    err = adc_oneshot_new_unit(&unit_config, &s_adc);
    if (err != ESP_OK) return err;

    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, s_channel, &channel_config);
    if (err != ESP_OK) return err;

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali);
    if (err != ESP_OK) return err;

    s_divider_x1000 = divider_x1000;
    return ESP_OK;
}

uint16_t battery_read_mv(void){
    if (s_cali == NULL) {
        return ESPNOW_BATTERY_UNKNOWN;
    }
    int sum = 0;
    for (int i = 0; i < BATTERY_SAMPLES; i++) {
        int mv;
        if (adc_oneshot_get_calibrated_result(s_adc, s_cali, s_channel, &mv) != ESP_OK) {
            return ESPNOW_BATTERY_UNKNOWN;
        }
        sum += mv;
    }
    uint32_t battery_mv = (uint32_t) sum / BATTERY_SAMPLES * s_divider_x1000 / 1000;
    /* Anything that does not fit is a wiring fault, not a battery. */
    return battery_mv >= ESPNOW_BATTERY_UNKNOWN ? ESPNOW_BATTERY_UNKNOWN : (uint16_t) battery_mv;
}
