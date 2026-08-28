#include "hcsr501.h"
#include "reedswitch.h"
#include "can.h"
#include "system_events.h"

void app_main(){

    QueueHandle_t events_queue = xQueueCreate(5, sizeof(sensor_event_t));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // ========== INICIALIZAÇÔES ==========
    ESP_ERROR_CHECK(reedswitch_init(events_queue));
    ESP_ERROR_CHECK(hcsr501_init(events_queue));
    ESP_ERROR_CHECK(can_init());
    ESP_ERROR_CHECK(sensors_task_init(events_queue));

}