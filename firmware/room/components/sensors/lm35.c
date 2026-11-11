#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lm35.h"
#include "sensor_events.h"
#include "board_pins.h"
#include "rtos_err.h"
#include "task_config.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

static const char *TAG = "lm35";

static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static adc_channel_t s_channel;

/* Average of LM35_SAMPLES calibrated readings, in mV. */
static esp_err_t read_mv(float *mv){
    int sum = 0;
    for (int i = 0; i < LM35_SAMPLES; i++) {
        int sample;
        esp_err_t err = adc_oneshot_get_calibrated_result(s_adc, s_cali, s_channel, &sample);
        if (err != ESP_OK) return err;
        sum += sample;
        vTaskDelay(pdMS_TO_TICKS(LM35_SAMPLE_GAP_MS));
    }
    *mv = (float) sum / LM35_SAMPLES;
    return ESP_OK;
}

static void lm35_task(void *arg){
    (void) arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        float mv;
        esp_err_t err = read_mv(&mv);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "ADC read failed: %s", esp_err_to_name(err));
        } else {
            float celsius = (mv - LM35_CHAIN_OFFSET_MV) / LM35_CHAIN_SLOPE_MV_PER_C;
            if (celsius < LM35_MIN_VALID_C || celsius > LM35_MAX_VALID_C) {
                ESP_LOGW(TAG, "%.0f mV -> %.1f C is out of the valid range, sensor fault?", mv, celsius);
            } else {
                ESP_LOGI(TAG, "%.0f mV -> %.2f C", mv, celsius);
                sensor_reading_t reading = {
                    .kind = SENSOR_KIND_TEMPERATURE,
                    .celsius = celsius,
                };
                sensor_events_emit(&reading);
            }
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LM35_PERIOD_MS));
    }
}

esp_err_t lm35_init(void){
    adc_unit_t unit;
    esp_err_t err = adc_oneshot_io_to_channel(PIN_LM35, &unit, &s_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO%d is not an ADC pin", PIN_LM35);
        return err;
    }
    if (unit != ADC_UNIT_1) {
        ESP_LOGE(TAG, "GPIO%d is on ADC2, which does not work with Wi-Fi on", PIN_LM35);
        return ESP_ERR_INVALID_ARG;
    }

    adc_oneshot_unit_init_cfg_t unit_config = {
        .unit_id = unit,
    };
    err = adc_oneshot_new_unit(&unit_config, &s_adc);
    if (err != ESP_OK) return err;

    /* 12 dB: the conditioned signal spans ~0.15 V (0 degC) to ~3 V (55 degC). */
    adc_oneshot_chan_cfg_t channel_config = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_oneshot_config_channel(s_adc, s_channel, &channel_config);
    if (err != ESP_OK) return err;

    /* The chain was fitted against real millivolts, so feed it calibrated ones, not raw * 3300 / 4095. */
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_curve_fitting(&cali_config, &s_cali);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADC calibration unavailable: %s", esp_err_to_name(err));
        return err;
    }

    /* Core 1 like the other inputs; low priority, a reading every 30 s has no deadline. */
    return esp_err_from_pdpass(xTaskCreatePinnedToCore(lm35_task, "lm35_task", STACK_SMALL, NULL,
                                                       PRIO_BAIXA, NULL, CORE_REALTIME));
}
