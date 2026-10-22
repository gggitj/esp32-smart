/**
 * @file board_pins.h
 * @brief Minimal ESP32-S3-N16R8 devkit board layer for the LinkIt SDK.
 */
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

#define BOOT_BTN_PIN 0

/* diagnostics component needs touch/I2C placeholders; this board has none. */
#define TP_I2C_PORT     0
#define TP_I2C_SCL      -1
#define TP_I2C_SDA      -1
#define TP_RST          -1
#define TP_INT          -1
#define TP_I2C_FREQ_HZ  400000

#endif
