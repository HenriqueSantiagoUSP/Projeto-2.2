#include "board_pins.h"
#include "can.h"
#include "hcsr501.h"
#include "reedswitch.h"
#include "sensor_events.h"

void app_main(void){

    QueueHandle_t events_queue = xQueueCreate(SENSOR_EVENT_QUEUE_DEPTH, sizeof(sensor_event_t));
    ESP_ERROR_CHECK(events_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    // ========== INITIALISATION ==========
    ESP_ERROR_CHECK(reedswitch_init(events_queue));
    ESP_ERROR_CHECK(hcsr501_init(events_queue));
    ESP_ERROR_CHECK(can_init());
    ESP_ERROR_CHECK(sensors_task_init(events_queue));

}
