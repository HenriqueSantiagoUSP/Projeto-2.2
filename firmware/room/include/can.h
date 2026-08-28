#ifndef __CAN_H__
#define __CAN_H__

#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "esp_err.h"

#include "map_pin.h"
#include "system_events.h"

#define CAN_GROUP_ID        0x0F0
#define SELF_TEST_ENABLE    1
#define SELF_TEST_DISABLE   0

#define CAN_TX_PIN          _CAN_TX_PIN
#define CAN_RX_PIN          _CAN_RX_PIN
#define CAN_BITRATE         200000
#define CAN_QUEUE_DEPTH     5
#define CAN_SELF_TEST       SELF_TEST_ENABLE

typedef enum {
    HCSR501_ID =       0x1,
    LM35_ID =          0x2,
    REEDSWITCH_ID =    0x3,

} can_sensor_id;

typedef struct {
    uint32_t id;
    uint8_t dados[8];
    uint8_t tamanho;
    uint32_t timestamp;
} event_can_t;

esp_err_t can_init(void);
esp_err_t can_transmit(uint8_t sensor_id, const uint8_t *send_buff, size_t len);

#endif