#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sensor_events.h"
#include "driver/gpio_filter.h"
#include "rtos_err.h"
#include "task_config.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"

static const char *TAG = "sensor_events";

#define SENSOR_QUEUE_DEPTH      32
/* sensors_task wakes at least this often, so it can feed the task watchdog. */
#define SENSOR_MAX_WAIT_MS      1000

typedef struct {
    gpio_num_t     gpio;
    sensor_kind_t  kind;
    uint8_t        index;
    int            active_level;
    int64_t        settle_us;
    int64_t        due_us;      /* when to read the settled level; 0 = nothing pending */
    int            reported;    /* last reported state: 1 active, 0 inactive, -1 never */
} sensor_input_t;

/* Written by sensor_events_add_input() before the inputs interrupt, then only by sensors_task. */
static sensor_input_t s_inputs[SENSOR_INPUT_MAX];
static volatile size_t s_input_count = 0;

static QueueHandle_t s_queue = NULL;
static sensor_listener_t s_listener = NULL;
static void *s_listener_ctx = NULL;

static volatile uint32_t s_dropped_events = 0;
static uint32_t s_reported_drops = 0;

static void IRAM_ATTR sensor_input_isr(void *arg){
    uint8_t slot = (uint8_t) (uintptr_t) arg;

    BaseType_t hp_task_woken = pdFALSE;
    if (xQueueSendFromISR(s_queue, &slot, &hp_task_woken) != pdTRUE) {
        s_dropped_events++;   /* reported by the task, never log from an ISR */
    }
    portYIELD_FROM_ISR(hp_task_woken);
}

/* Every edge postpones the read: the level is read once the input stops moving. */
static void input_edge(sensor_input_t *input, int64_t now_us){
    int64_t settled_at = now_us + input->settle_us;
    /* Never pull a pending read earlier, which would cut a warm-up short. */
    if (input->due_us == 0 || settled_at > input->due_us) {
        input->due_us = settled_at;
    }
}

static void input_settle(sensor_input_t *input){
    input->due_us = 0;

    int active = gpio_get_level(input->gpio) == input->active_level ? 1 : 0;
    if (active == input->reported) {
        return;   /* bounced back to where it was */
    }
    input->reported = active;

    sensor_reading_t reading = {
        .kind = input->kind,
        .index = input->index,
        .active = active,
    };
    sensor_events_emit(&reading);
}

static TickType_t ticks_until_next_due(int64_t now_us){
    int64_t next_us = INT64_MAX;
    for (size_t i = 0; i < s_input_count; i++) {
        if (s_inputs[i].due_us != 0 && s_inputs[i].due_us < next_us) {
            next_us = s_inputs[i].due_us;
        }
    }

    TickType_t wait = pdMS_TO_TICKS(SENSOR_MAX_WAIT_MS);
    if (next_us != INT64_MAX) {
        int64_t delta_ms = (next_us - now_us + 999) / 1000;
        TickType_t due = delta_ms <= 0 ? 0 : pdMS_TO_TICKS(delta_ms) + 1;
        if (due < wait) {
            wait = due;
        }
    }
    return wait;
}

static void sensors_task(void *arg){
    (void) arg;
    ESP_ERROR_CHECK(esp_task_wdt_add(NULL));

    while (1) {
        uint8_t slot;
        TickType_t wait = ticks_until_next_due(esp_timer_get_time());

        if (xQueueReceive(s_queue, &slot, wait) == pdTRUE && slot < s_input_count) {
            input_edge(&s_inputs[slot], esp_timer_get_time());
        }

        int64_t now_us = esp_timer_get_time();
        for (size_t i = 0; i < s_input_count; i++) {
            if (s_inputs[i].due_us != 0 && now_us >= s_inputs[i].due_us) {
                input_settle(&s_inputs[i]);
            }
        }

        uint32_t dropped = s_dropped_events;
        if (dropped != s_reported_drops) {
            ESP_LOGW(TAG, "%lu edge(s) dropped, queue full", dropped - s_reported_drops);
            s_reported_drops = dropped;
        }

        esp_task_wdt_reset();
    }
}

esp_err_t sensor_events_init(sensor_listener_t listener, void *ctx){
    if (listener == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    s_listener = listener;
    s_listener_ctx = ctx;

    s_queue = xQueueCreate(SENSOR_QUEUE_DEPTH, sizeof(uint8_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Core 1, high priority: the settle deadlines are what bounds the latency of a door. */
    return esp_err_from_pdpass(xTaskCreatePinnedToCore(sensors_task, "sensors_task", STACK_MEDIUM, NULL,
                                                       PRIO_ALTA, NULL, CORE_REALTIME));
}

esp_err_t sensor_events_add_input(gpio_num_t gpio, sensor_kind_t kind, uint8_t index,
                                  int active_level, uint32_t settle_ms, uint32_t ignore_ms){
    if (s_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_input_count >= SENSOR_INPUT_MAX) {
        ESP_LOGE(TAG, "input table full (%d)", SENSOR_INPUT_MAX);
        return ESP_ERR_NO_MEM;
    }

    /*
     * Hardware glitch filter (slide "ESP32 - GPIO"): pulses shorter than two IO-MUX
     * clocks never reach the ISR, so spikes picked up by long wires to a door cost no
     * interrupt at all. The settle time below still handles the contact bounce.
     */
    gpio_pin_glitch_filter_config_t filter_config = {
        .clk_src = GLITCH_FILTER_CLK_SRC_DEFAULT,
        .gpio_num = gpio,
    };
    gpio_glitch_filter_handle_t filter;
    esp_err_t err = gpio_new_pin_glitch_filter(&filter_config, &filter);
    if (err == ESP_OK) {
        err = gpio_glitch_filter_enable(filter);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "GPIO%d without glitch filter: %s", gpio, esp_err_to_name(err));
    }

    size_t slot = s_input_count;
    uint32_t first_read_ms = ignore_ms > settle_ms ? ignore_ms : settle_ms;
    s_inputs[slot] = (sensor_input_t) {
        .gpio = gpio,
        .kind = kind,
        .index = index,
        .active_level = active_level,
        .settle_us = (int64_t) settle_ms * 1000,
        .due_us = esp_timer_get_time() + (int64_t) first_read_ms * 1000,
        .reported = -1,
    };
    /* Publish the entry before the ISR can name its slot. */
    s_input_count = slot + 1;

    return gpio_isr_handler_add(gpio, sensor_input_isr, (void *) (uintptr_t) slot);
}

void sensor_events_emit(const sensor_reading_t *reading){
    if (s_listener != NULL) {
        s_listener(reading, s_listener_ctx);
    }
}
