#pragma once

/*
 * Pins of the room hub: what this firmware uses from the single PCB.
 *
 * The GPIO numbers live in pcb_pins.h (firmware/common/pcb), the map of the PCB both
 * boards share, checked at compile time against conflicts and reserved pins. This
 * file only picks and names what the hub needs; if the hub ever gets a PCB of its
 * own, its map goes here. Reasons for each pin: hardware/README.md.
 */

#include "pcb_pins.h"

#define PIN_HCSR501        PCB_PIN_PIR

/*
 * Doors, as many as the room needs: X(index, gpio, id, role).
 *  - index: 0, 1, 2... in order; door i is CAN sensor nibble 0x3 + i (up to 8).
 *  - id: names the MQTT topic porta/<id> and the door in the configuration.
 *  - role: EXIT, BATHROOM or OTHER. Only the default: config/desejada may change it.
 * Each reed switch closes to GND when the door is shut (internal pull-up), so a
 * high level means open. The PCB has 4 door inputs (PCB_PIN_DOOR1..4) and 4 more on
 * the expansion header (PCB_PIN_EXP1..4). To use door 3, add:
 *     X(2, PCB_PIN_DOOR3, "armario", OTHER)
 */
#define BOARD_DOORS(X)                                   \
    X(0, PCB_PIN_DOOR1, "quarto",   EXIT)                \
    X(1, PCB_PIN_DOOR2, "banheiro", BATHROOM)

/* LM35 after the LM358 stage: must be an ADC1 pin, ADC2 does not work with Wi-Fi on. */
#define PIN_LM35           PCB_PIN_LM35

#define PIN_CAN_TX         PCB_PIN_CAN_TX
#define PIN_CAN_RX         PCB_PIN_CAN_RX

/* No buzzer on this PCB: the local alarm is the status LED (indicators accepts NC). */
#define PIN_BUZZER         GPIO_NUM_NC

/* The DevKit's WS2812 (menuconfig → PCB selects the board version). */
#define PIN_STATUS_LED     PCB_PIN_STATUS_LED
