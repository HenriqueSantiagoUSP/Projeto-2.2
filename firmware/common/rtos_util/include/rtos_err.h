#pragma once

/*
 * FreeRTOS <-> esp_err_t glue.
 *
 * Lives on its own so that any module can map a FreeRTOS return code without
 * having to depend on an unrelated module just for this helper.
 */

#include "freertos/FreeRTOS.h"
#include "esp_err.h"

/* Maps a FreeRTOS pdPASS/pdFAIL return into an esp_err_t. */
static inline esp_err_t esp_err_from_pdpass(BaseType_t status){
    return (status == pdPASS) ? ESP_OK : ESP_FAIL;
}
