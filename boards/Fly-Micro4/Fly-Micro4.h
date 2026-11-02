/**
 * Fly-Micro4 (Mellow, RP2040) GPIO map
 *
 * Sources:
 *   pin.webp               (connector pinout)
 *   Fly-Micro4-schematic.pdf (net names / RP2040 pin numbers)
 *
 * GPIO0..GPIO29 are all assigned below. USB (GPIO? none), QSPI, BOOT
 * (QSPI_SS) and RUN are dedicated RP2040 pins and are not GPIOs.
 *
 * NOTE: GPIO12..GPIO15 are shared between the four driver DIAG outputs
 * and the 4-pin IO header (HV IN / IO0 / IO1 / IO2). Only one function
 * can be actively driven per pin.
 */

#ifndef BOARD_FLY_MICRO4_H
#define BOARD_FLY_MICRO4_H

#include <stdint.h>

/* ---- CAN0 (CAN L/H header) ---- */
#define CAN_RX_PIN              4
#define CAN_TX_PIN              5

/* ---- TMC2209 single-wire UART (shared by all drivers) ---- */
#define TMC_UART_PIN            9

/* ---- Stepper driver connectors ---- */
#define DRIVE0_STEP_PIN         6
#define DRIVE0_DIR_PIN          3
#define DRIVE0_EN_PIN           7
#define DRIVE0_DIAG_PIN         13
#define DRIVE0_UART_ADDRESS     0

#define DRIVE1_STEP_PIN         1
#define DRIVE1_DIR_PIN          0
#define DRIVE1_EN_PIN           2
#define DRIVE1_DIAG_PIN         14
#define DRIVE1_UART_ADDRESS     1

#define DRIVE2_STEP_PIN         26
#define DRIVE2_DIR_PIN          25
#define DRIVE2_EN_PIN           27
#define DRIVE2_DIAG_PIN         15
#define DRIVE2_UART_ADDRESS     2

#define DRIVE3_STEP_PIN         23
#define DRIVE3_DIR_PIN          22
#define DRIVE3_EN_PIN           24
#define DRIVE3_DIAG_PIN         12
#define DRIVE3_UART_ADDRESS     3

#define DRIVE_COUNT             4

typedef struct {
    uint8_t step;
    uint8_t dir;
    uint8_t en;
    uint8_t diag;
    uint8_t uart_address;
} fly_micro4_drive_t;

static const fly_micro4_drive_t FLY_MICRO4_DRIVES[DRIVE_COUNT] = {
    { DRIVE0_STEP_PIN, DRIVE0_DIR_PIN, DRIVE0_EN_PIN, DRIVE0_DIAG_PIN, DRIVE0_UART_ADDRESS },
    { DRIVE1_STEP_PIN, DRIVE1_DIR_PIN, DRIVE1_EN_PIN, DRIVE1_DIAG_PIN, DRIVE1_UART_ADDRESS },
    { DRIVE2_STEP_PIN, DRIVE2_DIR_PIN, DRIVE2_EN_PIN, DRIVE2_DIAG_PIN, DRIVE2_UART_ADDRESS },
    { DRIVE3_STEP_PIN, DRIVE3_DIR_PIN, DRIVE3_EN_PIN, DRIVE3_DIAG_PIN, DRIVE3_UART_ADDRESS },
};

/* ---- Left-side connectors ---- */
#define LASER_PIN               10      /* Laser PWM */
#define NEOPIXEL_PIN            11      /* RGB data */
#define HV_IN_PIN               12      /* HV IN / Drive3 DIAG */
#define IO0_PIN                 13      /* IO0 / Drive0 DIAG */
#define IO1_PIN                 14      /* IO1 / Drive1 DIAG */
#define IO2_PIN                 15      /* IO2 / Drive2 DIAG */

/* ---- Fans ---- */
#define FAN0_PIN                16
#define FAN1_PIN                17
#define FAN2_PIN                -1      /* FAN2 header has no GPIO (always on) */

/* ---- BLTouch ---- */
#define PROBE_PIN               18      /* BLTouch signal */
#define SERVO_PIN               19      /* BLTouch servo control */

/* ---- Heaters ---- */
#define HEAT0_PIN               20      /* HEAT0 / T0 */
#define BED_PIN                 21      /* BED heater */

/* ---- Temperatures (RP2040 ADC) ---- */
#define TEMP0_PIN               28      /* E0 / T0 thermistor, ADC2 */
#define TEMP_BED_PIN            29      /* Bed thermistor, ADC3 */

/* ---- On-board status LED ---- */
#define STATUS_LED_PIN          8

#endif /* BOARD_FLY_MICRO4_H */
