#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "sensor_events.h"
#include "hcsr501.h"
#include "reedswitch.h"
#include "rtos_err.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "sensor_events";

/* Debounce window per sensor: events arriving inside it are discarded. */
static const int64_t k_debounce_us[SENSOR_EVENT_COUNT] = {
    [SENSOR_EVENT_HCSR501]    = HCSR501_DEBOUNCE_MS    * 1000,
    [SENSOR_EVENT_REEDSWITCH] = REEDSWITCH_DEBOUNCE_MS * 1000,
};

static void sensors_task(void *arg){
    QueueHandle_t queue = (QueueHandle_t) arg;
    int64_t last_event_us[SENSOR_EVENT_COUNT] = {0};
    sensor_event_t evt;

    while (1) {
        if (xQueueReceive(queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (evt.type >= SENSOR_EVENT_COUNT) {
            continue;
        }

        /*
         * A mechanical contact bounces for a few ms, firing a burst of interrupts
         * for a single physical actuation. Only the first event of a burst is
         * handled; the handler then re-reads the GPIO, already settled.
         */
        int64_t now = esp_timer_get_time();
        if (last_event_us[evt.type] != 0 &&
            now - last_event_us[evt.type] < k_debounce_us[evt.type]) {
            continue;
        }
        last_event_us[evt.type] = now;

        switch (evt.type) {
            case SENSOR_EVENT_HCSR501:
                hcsr501_handle_event(&evt);
                break;
            case SENSOR_EVENT_REEDSWITCH:
                reedswitch_handle_event(&evt);
                break;
            case SENSOR_EVENT_COUNT:
                break;
        }
    }
}

esp_err_t sensors_task_init(QueueHandle_t events_queue){
    if (events_queue == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "creating sensors_task");
    return esp_err_from_pdpass(xTaskCreate(sensors_task, "sensors_task", 4096, events_queue, 10, NULL));
}
