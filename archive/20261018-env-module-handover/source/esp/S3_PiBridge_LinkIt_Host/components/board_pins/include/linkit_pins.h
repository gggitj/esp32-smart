/**
 * @file linkit_pins.h
 * @brief Pogo/UART pins for two ESP32-S3-N16R8 devkits.
 *
 * Both boards use the same pins; the cable crosses TX and RX.
 * GPIO21 is used for DET (19/20 are wired to the native USB on devkits).
 */
#ifndef LINKIT_PINS_H
#define LINKIT_PINS_H

#include "driver/uart.h"
#include "driver/spi_master.h"
#include "board_pins.h"

#define POGO_UART_NUM   UART_NUM_1
#define POGO_PIN_TX     17
#define POGO_PIN_RX     18
#define POGO_PIN_DET    21

/* SPI extension placeholder (not used in this bring-up). */
#define LINKIT_SPI_HOST SPI2_HOST
#define LINKIT_SPI_MOSI -1
#define LINKIT_SPI_SCLK -1
#define LINKIT_SPI_MISO -1
#define LINKIT_SPI_CS   -1

#endif
