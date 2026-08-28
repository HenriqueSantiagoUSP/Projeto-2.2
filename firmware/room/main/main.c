#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_err.h"

#include "board_pins.h"
#include "can.h"
#include "can_ids.h"
#include "hcsr501.h"
#include "reedswitch.h"
#include "sensor_events.h"
#include "esp_log.h"

static const char *TAG = "main";

/* Logs the bus directory once, so a boot log shows which ids we can resolve. */
static void log_bus_map(void){
    size_t group_count = 0;
    const can_group_desc_t *groups = can_ids_groups(&group_count);

    for (size_t g = 0; g < group_count; g++) {
        for (uint8_t s = 0; s < groups[g].sensor_count; s++) {
            const can_sensor_desc_t *sensor = &groups[g].sensors[s];
            ESP_LOGI(TAG, "0x%03X  %s/%s",
                     (unsigned) CAN_MSG_ID(groups[g].group, sensor->nibble),
                     groups[g].name, sensor->name);
        }
    }
}

/* Any door or window opening on the bus, whoever reports it. */
static void on_contact(const can_rx_event_t *event, const can_id_info_t *info, void *ctx){
    (void) ctx;

    if (event->length < 1) {
        return;
    }
    ESP_LOGI(TAG, "%s/%s is %s",
             info->group->name, info->sensor->name,
             event->data[0] ? "OPEN" : "CLOSED");
}

void app_main(void){

    QueueHandle_t events_queue = xQueueCreate(SENSOR_EVENT_QUEUE_DEPTH, sizeof(sensor_event_t));
    ESP_ERROR_CHECK(events_queue != NULL ? ESP_OK : ESP_ERR_NO_MEM);

    ESP_ERROR_CHECK(gpio_install_isr_service(0));

    log_bus_map();

    /* Handlers must be registered before can_init(), which starts can_task. */
    ESP_ERROR_CHECK(can_rx_handler_register(CAN_GROUP_ANY, CAN_SENSOR_KIND_CONTACT,
                                            on_contact, NULL));

    // ========== INITIALISATION ==========
    ESP_ERROR_CHECK(reedswitch_init(events_queue));
    ESP_ERROR_CHECK(hcsr501_init(events_queue));
    ESP_ERROR_CHECK(can_init());
    ESP_ERROR_CHECK(sensors_task_init(events_queue));

}
