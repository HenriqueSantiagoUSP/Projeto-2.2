#pragma once

/*
 * Pin map of the single PCB shared by the room hub and the wearable.
 *
 * First project: one board carries the peripherals of both, and each firmware uses
 * its part (the board_pins.h of each project picks and renames what it needs). So no
 * two functions may share a GPIO, and no function may sit on a pin that a feature
 * turned on later would take away. hardware/README.md has the table and the reasons;
 * this header is the firmware's single source of truth, checked at compile time.
 *
 * The PCB is a carrier for an ESP32-S3-DevKitC-1: the RGB LED and the USB ports are
 * the DevKit's own.
 *
 * Never used (rejected below):
 *  - GPIO0, 3, 45, 46: strapping pins, their level at reset selects the boot mode;
 *  - GPIO19, 20: USB-Serial-JTAG (console, flashing and debugging over USB);
 *  - GPIO43, 44: UART0, the console through the DevKit's USB-UART bridge;
 *  - GPIO26-37: SPI flash and, on N8R8/N16R8 modules, the octal PSRAM;
 *  - GPIO38, 48: the DevKit's WS2812 (v1.1 / v1.0). GPIO47-48 are SPICLK_P/N, powered
 *    by VDD_SPI: 1.8 V on modules with 1.8 V memory.
 * Reserved for features that may be turned on later (rejected below):
 *  - GPIO15, 16: XTAL_32K_P/N, an external 32 kHz crystal (accurate RTC in sleep).
 * Expansion only:
 *  - GPIO39-42: MTCK/MTDO/MTDI/MTMS, JTAG over pins. Debugging goes over USB today; a
 *    function placed here is lost if JTAG over pins is ever enabled.
 * Other rules:
 *  - analog inputs only on ADC1 (GPIO1-10): ADC2 is unavailable while Wi-Fi runs, and
 *    ADC1 pins are spared for analog where possible;
 *  - I2C on GPIO1 (SDA) / GPIO2 (SCL): the only pins the ULP RISC-V RTC I2C accepts
 *    apart from strapping pins, so the ULP can read the MPU-6050 in deep sleep later.
 *    If the traces to the GY-521 cross, rotate the module on the board instead of
 *    swapping these;
 *  - wake-up inputs (button, MPU INT) on RTC GPIOs (GPIO0-21), for ext1 wake-up.
 */

#include "driver/gpio.h"

/* Analog block (ADC1), top of header J1, next to 3V3. */
#define PCB_PIN_LM35            GPIO_NUM_4    /* hub: LM35 after the LM358 stage, ADC1_CH3  */
#define PCB_PIN_BATTERY         GPIO_NUM_5    /* reserved, ADC1_CH4: battery divider of a future
                                                 wearable board; not fitted on this PCB, which
                                                 runs the wearable from a power bank        */
#define PCB_PIN_ANALOG_SPARE1   GPIO_NUM_6    /* free, ADC1_CH5                             */
#define PCB_PIN_ANALOG_SPARE2   GPIO_NUM_7    /* free, ADC1_CH6                             */

/* I2C and the MPU-6050 (wearable). External pull-ups: the GY-521 module has them. */
#define PCB_PIN_I2C_SDA         GPIO_NUM_1
#define PCB_PIN_I2C_SCL         GPIO_NUM_2
#define PCB_PIN_MPU_INT         GPIO_NUM_17   /* RTC GPIO: wakes the wearable from deep sleep */

/* Room inputs (hub), towards the cable connectors. Doors 3 and 4 are optional and sit
   on ADC1 pins, so they give way if analog inputs ever run short. */
#define PCB_PIN_PIR             GPIO_NUM_8
#define PCB_PIN_DOOR1           GPIO_NUM_11
#define PCB_PIN_DOOR2           GPIO_NUM_12
#define PCB_PIN_DOOR3           GPIO_NUM_9
#define PCB_PIN_DOOR4           GPIO_NUM_10

/* User I/O. There is no buzzer on this PCB (dropped 2026-10-23): alerts show on the
   status LED and reach the caregiver through the central. GPIO21 is left free. */
#define PCB_PIN_BUTTON          GPIO_NUM_18   /* wearable: "estou bem" to GND; RTC GPIO     */

/* CAN transceiver, at the bottom of J1 next to 5V/GND: close to the board edge and far
   from the analog block. */
#define PCB_PIN_CAN_TX          GPIO_NUM_13
#define PCB_PIN_CAN_RX          GPIO_NUM_14

/* Expansion header (for instance doors 5 to 8), only while JTAG over pins is unused. */
#define PCB_PIN_EXP1            GPIO_NUM_39
#define PCB_PIN_EXP2            GPIO_NUM_40
#define PCB_PIN_EXP3            GPIO_NUM_41
#define PCB_PIN_EXP4            GPIO_NUM_42

/* On the DevKit itself, not a trace of the PCB. */
#ifdef CONFIG_PCB_DEVKIT_V1_1
#define PCB_PIN_STATUS_LED      GPIO_NUM_38
#else
#define PCB_PIN_STATUS_LED      GPIO_NUM_48
#endif

/* ---------------------------------------------------------------------------
 * Compile-time checks
 * ------------------------------------------------------------------------- */

/* Every GPIO the PCB routes: X(name, pin, kind). */
#define PCB_PINS(X)                                 \
    X(LM35,          PCB_PIN_LM35,          ANALOG) \
    X(BATTERY,       PCB_PIN_BATTERY,       ANALOG) \
    X(ANALOG_SPARE1, PCB_PIN_ANALOG_SPARE1, ANALOG) \
    X(ANALOG_SPARE2, PCB_PIN_ANALOG_SPARE2, ANALOG) \
    X(I2C_SDA,       PCB_PIN_I2C_SDA,       ULP_SDA) \
    X(I2C_SCL,       PCB_PIN_I2C_SCL,       ULP_SCL) \
    X(MPU_INT,       PCB_PIN_MPU_INT,       WAKE)   \
    X(PIR,           PCB_PIN_PIR,           DIGITAL) \
    X(DOOR1,         PCB_PIN_DOOR1,         DIGITAL) \
    X(DOOR2,         PCB_PIN_DOOR2,         DIGITAL) \
    X(DOOR3,         PCB_PIN_DOOR3,         DIGITAL) \
    X(DOOR4,         PCB_PIN_DOOR4,         DIGITAL) \
    X(BUTTON,        PCB_PIN_BUTTON,        WAKE)   \
    X(CAN_TX,        PCB_PIN_CAN_TX,        DIGITAL) \
    X(CAN_RX,        PCB_PIN_CAN_RX,        DIGITAL) \
    X(EXP1,          PCB_PIN_EXP1,          EXPANSION) \
    X(EXP2,          PCB_PIN_EXP2,          EXPANSION) \
    X(EXP3,          PCB_PIN_EXP3,          EXPANSION) \
    X(EXP4,          PCB_PIN_EXP4,          EXPANSION)

#define PCB_GPIO_NEVER(p)     ((p) == 0 || (p) == 3 || (p) == 45 || (p) == 46 ||   \
                               (p) == 19 || (p) == 20 || (p) == 43 || (p) == 44 || \
                               ((p) >= 26 && (p) <= 38) || (p) == 47 || (p) == 48)
#define PCB_GPIO_RESERVED(p)  ((p) == 15 || (p) == 16)
#define PCB_GPIO_JTAG(p)      ((p) >= 39 && (p) <= 42)
#define PCB_GPIO_ADC1(p)      ((p) >= 1 && (p) <= 10)
#define PCB_GPIO_RTC(p)       ((p) <= 21)    /* GPIO0-21; the list never holds negatives */

/* What each kind of function needs from its pin. */
#define PCB_OK_ANALOG(p)      PCB_GPIO_ADC1(p)
#define PCB_OK_DIGITAL(p)     (!PCB_GPIO_JTAG(p))
#define PCB_OK_WAKE(p)        (PCB_GPIO_RTC(p) && !PCB_GPIO_JTAG(p))
#define PCB_OK_ULP_SDA(p)     ((p) == 1)
#define PCB_OK_ULP_SCL(p)     ((p) == 2)
#define PCB_OK_EXPANSION(p)   PCB_GPIO_JTAG(p)

#define PCB_CHECK_PIN(name, pin, kind)                                                     \
    _Static_assert(!PCB_GPIO_NEVER(pin) && !PCB_GPIO_RESERVED(pin),                        \
                   "PCB_PIN_" #name " is on a forbidden or reserved GPIO (see pcb_pins.h)"); \
    _Static_assert(PCB_OK_##kind(pin), "PCB_PIN_" #name " is on a GPIO unfit for " #kind);

PCB_PINS(PCB_CHECK_PIN)

/* One bit per pin: OR and sum only agree when no two functions share a GPIO. */
#define PCB_PIN_OR(name, pin, kind)    | (1ULL << (pin))
#define PCB_PIN_SUM(name, pin, kind)   + (1ULL << (pin))
_Static_assert((0ULL PCB_PINS(PCB_PIN_OR)) == (0ULL PCB_PINS(PCB_PIN_SUM)),
               "two functions share a GPIO in PCB_PINS (pcb_pins.h)");
