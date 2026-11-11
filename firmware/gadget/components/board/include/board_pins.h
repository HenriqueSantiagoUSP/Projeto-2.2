#pragma once

/*
 * Pins of the wearable: what this firmware uses from the single PCB.
 *
 * The GPIO numbers live in pcb_pins.h (firmware/common/pcb), the map of the PCB both
 * boards share, checked at compile time against conflicts and reserved pins. This
 * file only picks and names what the wearable needs; when the wearable gets a board
 * of its own (the real wristband), its map goes here. Reasons for each pin:
 * hardware/README.md.
 */

#include "pcb_pins.h"

/* MPU-6050 (GY-521) on I2C, 400 kHz. GPIO1/2 are the pins the ULP RTC I2C can use too. */
#define PIN_I2C_SDA         PCB_PIN_I2C_SDA
#define PIN_I2C_SCL         PCB_PIN_I2C_SCL
/* MPU-6050 INT: wired for a future wake-up from sleep (an RTC GPIO); the FIFO is polled for now. */
#define PIN_MPU_INT         PCB_PIN_MPU_INT

/* "Estou bem" button to GND (internal pull-up); an RTC GPIO, so it can wake the wearable. */
#define PIN_BUTTON          PCB_PIN_BUTTON
/* No buzzer on this PCB: the cancel window shows on the status LED (indicators accepts NC). */
#define PIN_BUZZER          GPIO_NUM_NC
/* Battery voltage through a divider; must be an ADC1 pin (ADC2 does not work with the radio on).
   Not fitted on the prototype PCB (power bank): read only with CONFIG_GADGET_BATTERY_SENSE. */
#define PIN_BATTERY         PCB_PIN_BATTERY

/* The DevKit's WS2812 (menuconfig → PCB selects the board version). */
#define PIN_STATUS_LED      PCB_PIN_STATUS_LED

/*
 * CAN transceiver, only with CONFIG_GADGET_CAN: bench tests and the course
 * requirement. In use the wearable talks to the hub over ESP-NOW and has no
 * transceiver. Same pins as the hub: it is the same PCB.
 */
#define PIN_CAN_TX          PCB_PIN_CAN_TX
#define PIN_CAN_RX          PCB_PIN_CAN_RX
