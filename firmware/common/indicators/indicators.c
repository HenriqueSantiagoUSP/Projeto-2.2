#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "indicators.h"
#include "led_strip.h"
#include "rtos_err.h"
#include "task_config.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "indicators";

#define IND_TICK_MS          50
#define IND_BRIGHTNESS       16    /* of 255: the bare LED is very bright at night */

/* Set by indicators_init() before the task starts; then only read. */
static led_strip_handle_t s_led = NULL;
static gpio_num_t s_buzzer_gpio = GPIO_NUM_NC;

/*
 * Wanted state: written by any task, read by indicators_task. A 64-bit deadline is
 * not written in one instruction on this CPU, so every access goes through a short
 * critical section (portMUX: the other core may be writing at the same time).
 */
typedef struct {
    ind_status_t status;
    bool         buzzer_on;
    int64_t      buzzer_until_us;   /* 0 = until stopped */
} ind_state_t;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static ind_state_t s_state = { .status = IND_STATUS_BOOT };

static void led_show(uint8_t r, uint8_t g, uint8_t b){
    if (s_led == NULL) {
        return;
    }
    led_strip_set_pixel(s_led, 0, r, g, b);
    led_strip_refresh(s_led);
}

static void led_update(ind_status_t status, int64_t now_ms){
    switch (status) {
        case IND_STATUS_BOOT:
            led_show(0, 0, IND_BRIGHTNESS);
            break;
        case IND_STATUS_OK:
            if (now_ms % 1000 < 100) led_show(0, IND_BRIGHTNESS, 0); else led_show(0, 0, 0);
            break;
        case IND_STATUS_NO_NETWORK:
            if (now_ms % 1000 < 100) led_show(IND_BRIGHTNESS, IND_BRIGHTNESS, 0); else led_show(0, 0, 0);
            break;
        case IND_STATUS_ALERT:
            if (now_ms % 250 < 125) led_show(IND_BRIGHTNESS, 0, 0); else led_show(0, 0, 0);
            break;
        case IND_STATUS_FAULT:
            if (now_ms % 2000 < 500) led_show(IND_BRIGHTNESS, 0, 0); else led_show(0, 0, 0);
            break;
    }
}

static void indicators_task(void *arg){
    (void) arg;
    TickType_t last_wake = xTaskGetTickCount();

    while (1) {
        int64_t now_us = esp_timer_get_time();

        /* Copy under the lock; the slow LED and GPIO work happens outside it. */
        taskENTER_CRITICAL(&s_lock);
        if (s_state.buzzer_on && s_state.buzzer_until_us != 0 && now_us >= s_state.buzzer_until_us) {
            s_state.buzzer_on = false;
        }
        ind_state_t state = s_state;
        taskEXIT_CRITICAL(&s_lock);

        led_update(state.status, now_us / 1000);
        if (s_buzzer_gpio != GPIO_NUM_NC) {
            gpio_set_level(s_buzzer_gpio, state.buzzer_on && ((now_us / 1000) % 500 < 250));
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(IND_TICK_MS));
    }
}

esp_err_t indicators_init(const indicators_config_t *config){
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (config->led_gpio != GPIO_NUM_NC) {
        led_strip_config_t strip_config = {
            .strip_gpio_num = config->led_gpio,
            .max_leds = 1,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        };
        led_strip_rmt_config_t rmt_config = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10 * 1000 * 1000,
        };
        esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led);
        if (err != ESP_OK) {
            /* Signalling is a convenience: keep running without it. */
            ESP_LOGW(TAG, "status LED unavailable: %s", esp_err_to_name(err));
            s_led = NULL;
        }
    }

    if (config->buzzer_gpio != GPIO_NUM_NC) {
        gpio_config_t buzzer_config = {
            .pin_bit_mask = 1ULL << config->buzzer_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        esp_err_t err = gpio_config(&buzzer_config);
        if (err != ESP_OK) return err;
        gpio_set_level(config->buzzer_gpio, 0);
        s_buzzer_gpio = config->buzzer_gpio;
    }

    return esp_err_from_pdpass(xTaskCreatePinnedToCore(indicators_task, "indicators", STACK_SMALL, NULL,
                                                       PRIO_MINIMA, NULL, CORE_PROTOCOL));
}

void indicators_set_status(ind_status_t status){
    taskENTER_CRITICAL(&s_lock);
    s_state.status = status;
    taskEXIT_CRITICAL(&s_lock);
}

void indicators_buzzer_start(uint32_t duration_ms){
    int64_t until_us = duration_ms ? esp_timer_get_time() + (int64_t) duration_ms * 1000 : 0;
    taskENTER_CRITICAL(&s_lock);
    s_state.buzzer_until_us = until_us;
    s_state.buzzer_on = true;
    taskEXIT_CRITICAL(&s_lock);
}

void indicators_buzzer_stop(void){
    taskENTER_CRITICAL(&s_lock);
    s_state.buzzer_on = false;
    taskEXIT_CRITICAL(&s_lock);
}
