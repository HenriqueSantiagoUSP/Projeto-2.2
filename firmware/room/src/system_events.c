#include "system_events.h"

static const char* TAG = "SYSTEM EVENTS";

static void sensors_task(void *arg){
    QueueHandle_t queue = (QueueHandle_t) arg;
    sensor_event_t evt;

    while(1){
        if (xQueueReceive(queue, &evt, portMAX_DELAY)){
            switch (evt.type){
                case REEDSWITCH_EVENT:
                    reedswitch_task_callback(evt);
                    break;
                case HCSR501_EVENT:
                    hcsr501_task_callback();
                    break;
            }
        }
    }

}

esp_err_t sensors_task_init(QueueHandle_t events_queue){
    ESP_LOGI(TAG, "criando sensors_task");
    return pd_check(xTaskCreate(sensors_task, "sensors_task", 4096, events_queue, 10, NULL));
}

esp_err_t pd_check(BaseType_t arg){
    if (arg != pdPASS){
        return ESP_FAIL;
    }

    return ESP_OK;
}