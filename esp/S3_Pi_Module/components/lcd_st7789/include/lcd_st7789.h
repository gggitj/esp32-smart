#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* 1.54" IPS 240x240, ST7789, 4-wire SPI.
 * SCLK=GPIO12 MOSI=GPIO11 CS=GPIO10 BLK=GPIO13.
 * This module's DC/RES pads are swapped: the driver uses GPIO8 as DC and
 * GPIO9 as RES (verified on hardware). */
esp_err_t lcd_st7789_init(void);   /* GPIO + SPI bus */
esp_err_t lcd_st7789_start(void);  /* panel reset/init/on */
void lcd_st7789_set_ready_cb(void (*cb)(void));
// Native numeric RGB565 input; driver converts once to big-endian wire bytes.
// Completion callback fires once per tile, from the calling task, after DMA.
void lcd_st7789_flush(int x1, int y1, int x2, int y2, const void *pixels);
void lcd_st7789_fill(uint16_t rgb565);
void lcd_st7789_backlight(bool on);
void lcd_st7789_diag_next(void);
void lcd_st7789_color_bars(void);
