#include "lcd_st7789.h"
#include "lcd_rgb565.h"
#include "esp_heap_caps.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

#define LCD_HOST      SPI2_HOST
#define PIN_NUM_SCLK  12
#define PIN_NUM_MOSI  11
#define PIN_NUM_CS    10
/* This module's silkscreen has DC and RES swapped relative to the silicon:
 * driving GPIO8 as DC and GPIO9 as RES is what actually produces a picture
 * (verified on hardware 2026-10-17). */
#define PIN_NUM_DC    8
#define PIN_NUM_RST   9
#define PIN_NUM_BLK   13

#define LCD_H_RES     240
#define LCD_V_RES     240

static const char *TAG = "LCD_ST7789";
static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_io;
static SemaphoreHandle_t s_flush_done;
static SemaphoreHandle_t s_flush_lock;
static uint8_t *s_wire_line;
static void (*s_ready_cb)(void);

static esp_err_t panel_bringup_impl(int spi_mode, int gap_y, int dc, int cs, int rst);

static bool IRAM_ATTR on_color_done(esp_lcd_panel_io_handle_t io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)io;
    (void)edata;
    (void)user_ctx;
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done, &woken);
    return woken == pdTRUE;
}

esp_err_t lcd_st7789_init(void)
{
    if (!s_flush_done) {
        s_flush_done = xSemaphoreCreateBinary();
        if (!s_flush_done) return ESP_ERR_NO_MEM;
    }
    if (!s_flush_lock) s_flush_lock = xSemaphoreCreateMutex();
    if (!s_wire_line) s_wire_line = heap_caps_malloc(LCD_H_RES * 2, MALLOC_CAP_DMA);
    if (!s_flush_lock || !s_wire_line) return ESP_ERR_NO_MEM;

    const gpio_config_t blk_cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_BLK,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&blk_cfg), TAG, "backlight gpio");
    gpio_set_level(PIN_NUM_BLK, 1);

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_NUM_SCLK,
        .mosi_io_num = PIN_NUM_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 40 * sizeof(uint16_t),
    };
    esp_err_t err = spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }
    /* The version that first lit the panel did this first pass here. */
    return panel_bringup_impl(0, 0, 9, 10, 8);
}

esp_err_t lcd_st7789_start(void)
{
    return panel_bringup_impl(0, 0, 8, 10, 9);
}

esp_err_t panel_bringup_impl(int spi_mode, int gap_y, int dc, int cs, int rst)
{
    esp_lcd_panel_io_spi_config_t io = {
        .dc_gpio_num = dc,
        .cs_gpio_num = cs,
        .pclk_hz = 1 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 1,
        .on_color_trans_done = on_color_done,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &s_io),
                        TAG, "panel io");

    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = rst,
        .rgb_endian = LCD_RGB_ENDIAN_RGB,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_io, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_panel, false), TAG, "swap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_panel, false, false), TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_set_gap(s_panel, 0, 0), TAG, "gap");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "on");
    ESP_LOGI(TAG, "ST7789 init pass: SCLK=%d MOSI=%d CS=%d DC=%d RST=%d BLK=%d",
             PIN_NUM_SCLK, PIN_NUM_MOSI, cs, dc, rst, PIN_NUM_BLK);
    return ESP_OK;
}

void lcd_st7789_set_ready_cb(void (*cb)(void))
{
    s_ready_cb = cb;
}

void lcd_st7789_backlight(bool on)
{
    gpio_set_level(PIN_NUM_BLK, on ? 1 : 0);
}

void lcd_st7789_flush(int x1, int y1, int x2, int y2, const void *pixels)
{
    if (!s_panel || !pixels || x1 < 0 || y1 < 0 || x2 < x1 || y2 < y1 ||
        x2 >= LCD_H_RES || y2 >= LCD_V_RES) {
        ESP_LOGE(TAG, "Invalid LCD flush rectangle/state");
        if (s_ready_cb) s_ready_cb();
        return;
    }
    xSemaphoreTake(s_flush_lock, portMAX_DELAY);
    const uint16_t *source = pixels;
    const size_t width = (size_t)(x2 - x1 + 1);
    for (int y = y1; y <= y2; ++y) {
        lcd_rgb565_pack_be(s_wire_line, source, width);
        const esp_err_t err = esp_lcd_panel_draw_bitmap(s_panel, x1, y, x2 + 1, y + 1, s_wire_line);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "LCD row submission failed: %s", esp_err_to_name(err));
            break;
        }
        // The ISR acknowledges a row only. Do not release the LVGL tile until
        // ALL rows have completed, or its antialiased glyph buffer can change
        // while DMA is reading it. The shared staging line also stays owned.
        xSemaphoreTake(s_flush_done, portMAX_DELAY);
        source += width;
    }
    xSemaphoreGive(s_flush_lock);
    if (s_ready_cb) s_ready_cb();
}

void lcd_st7789_fill(uint16_t rgb565)
{
    static uint16_t line[LCD_H_RES];
    if (!s_panel) return;
    for (int i = 0; i < LCD_H_RES; ++i) line[i] = rgb565;
    for (int y = 0; y < LCD_V_RES; ++y) lcd_st7789_flush(0, y, LCD_H_RES - 1, y, line);
}

/* Rebuilt from the version that first lit the panel: one configuration per
 * boot, step kept in NVS, press RST to advance. */
void lcd_st7789_diag_next(void)
{
    static const struct {
        int spi_mode;
        int gap_y;
        int dc, cs, rst;
        uint16_t color;
        const char *name;
    } cfgs[] = {
        { 0, 0,  9, 10, 8, 0xF800, "RED    dc9 cs10 rst8" },
        { 0, 0, 10,  9, 8, 0x07E0, "GREEN  dc10 cs9 rst8" },
        { 0, 0,  8, 10, 9, 0x001F, "BLUE   dc8 cs10 rst9" },
        { 0, 0,  9,  8, 10, 0xFFFF, "WHITE  dc9 cs8 rst10" },
        { 3, 0,  9, 10, 8, 0xFFE0, "YELLOW mode3 dc9 cs10 rst8" },
    };
    const unsigned count = sizeof(cfgs) / sizeof(cfgs[0]);
    uint32_t step = 0;

    nvs_handle_t h;
    if (nvs_open("lcdtest", NVS_READWRITE, &h) == ESP_OK) {
        nvs_get_u32(h, "step", &step);
        nvs_set_u32(h, "step", step + 1);
        nvs_commit(h);
        nvs_close(h);
    }
    const unsigned i = step % count;

    if (panel_bringup_impl(cfgs[i].spi_mode, cfgs[i].gap_y,
                           cfgs[i].dc, cfgs[i].cs, cfgs[i].rst) != ESP_OK) {
        ESP_LOGE(TAG, "diag step %u bringup failed", i);
        return;
    }
    lcd_st7789_fill(cfgs[i].color);
    ESP_LOGW(TAG, "BOOT DIAG step=%u/%u mode=%d dc=%d cs=%d rst=%d -> %s",
             i, count - 1, cfgs[i].spi_mode, cfgs[i].dc, cfgs[i].cs, cfgs[i].rst, cfgs[i].name);
}

/* Three horizontal bars: top RED, middle GREEN, bottom BLUE (RGB565). */
void lcd_st7789_color_bars(void)
{
    static uint16_t line[LCD_H_RES];
    static const uint16_t cols[3] = { 0xF800, 0x07E0, 0x001F };
    if (!s_panel) return;
    for (int y = 0; y < LCD_V_RES; ++y) {
        const uint16_t c = cols[y / (LCD_V_RES / 3)];
        for (int i = 0; i < LCD_H_RES; ++i) line[i] = c;
        lcd_st7789_flush(0, y, LCD_H_RES - 1, y, line);
    }
}
