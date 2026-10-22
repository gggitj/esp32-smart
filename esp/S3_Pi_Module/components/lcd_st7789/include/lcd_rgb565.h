#pragma once
#include <stddef.h>
#include <stdint.h>

// Input is numeric/native RGB565, never an already byte-swapped LVGL buffer.
// ST7789 SPI expects the most-significant byte first.
static inline void lcd_rgb565_pack_be(uint8_t *output, const uint16_t *input, size_t count)
{
    for (size_t i = 0; i < count; ++i) {
        output[2 * i] = (uint8_t)(input[i] >> 8);
        output[2 * i + 1] = (uint8_t)input[i];
    }
}
