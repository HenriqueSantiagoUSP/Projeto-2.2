#include "sys_events.h"

/* Static storage: the group exists from sys_events_init() on and is never freed. */
static StaticEventGroup_t s_storage;
static EventGroupHandle_t s_group = NULL;

esp_err_t sys_events_init(void){
    if (s_group == NULL) {
        s_group = xEventGroupCreateStatic(&s_storage);
    }
    return s_group != NULL ? ESP_OK : ESP_FAIL;
}

void sys_events_set(EventBits_t bits){
    if (s_group != NULL) {
        xEventGroupSetBits(s_group, bits);
    }
}

void sys_events_clear(EventBits_t bits){
    if (s_group != NULL) {
        xEventGroupClearBits(s_group, bits);
    }
}

bool sys_events_all(EventBits_t bits){
    if (s_group == NULL) {
        return false;
    }
    return (xEventGroupGetBits(s_group) & bits) == bits;
}

bool sys_events_wait_all(EventBits_t bits, TickType_t timeout){
    if (s_group == NULL) {
        return false;
    }
    /* pdFALSE: never clear on exit, the bits are state, not one-shot events. */
    EventBits_t got = xEventGroupWaitBits(s_group, bits, pdFALSE, pdTRUE, timeout);
    return (got & bits) == bits;
}
