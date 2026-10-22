#include "ui_env.h"
#include "ui_env_view.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_st7789.h"
#include "pi_sensor_device.h"
#include "power_guard.h"
#include <stdlib.h>

#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP
#error "LCD driver owns RGB565 wire conversion: use LV_COLOR_DEPTH=16 and LV_COLOR_16_SWAP=0"
#endif

#define UI_W 240
#define UI_H 240
#define BUF_LINES 16
static lv_disp_draw_buf_t draw;
static lv_disp_drv_t driver;
static void flush_ready(void) { lv_disp_flush_ready(&driver); }
static void flush_cb(lv_disp_drv_t *d, const lv_area_t *area, lv_color_t *pixels)
{
    (void)d;
    lcd_st7789_flush(area->x1, area->y1, area->x2, area->y2, pixels);
}
static void tick_cb(void *arg) { (void)arg; lv_tick_inc(2); }

static void ui_task(void *arg)
{
    (void)arg;
    TickType_t last = 0;
    for (;;) {
        lv_timer_handler();
        const TickType_t now = xTaskGetTickCount();
        if (now - last >= pdMS_TO_TICKS(1000)) {
            last = now;
            pi_sensor_sample_t s = {0};
            bool fresh = pi_sensor_device_latest(&s) && esp_timer_get_time() - s.sample_time_us < 5000000;
            const char *source = power_guard_source();
            const ui_env_snapshot_t view = {
                .temperature_valid = fresh && s.temperature_valid,
                .humidity_valid = fresh && s.humidity_valid,
                .pressure_valid = fresh && s.pressure_valid,
                .light_valid = fresh && s.light_valid, .battery_valid = fresh && s.battery_valid,
                .temperature = s.temperature_c, .humidity = s.humidity_rh,
                .pressure = s.pressure_hpa, .light = s.light_lux,
                .battery_volts = s.battery_volts, .battery_percent = s.battery_percent,
                .power_source = source[0] == 'e' ? 1 : source[0] == 'b' ? 0 : -1,
            };
            ui_env_view_update(&view);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ui_env_start(void)
{
    lv_init();
    lv_color_t *a = heap_caps_malloc(UI_W * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_color_t *b = heap_caps_malloc(UI_W * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!a || !b) {
        free(a); free(b); ESP_LOGE("UI_ENV", "LCD draw buffer allocation failed"); return;
    }
    lv_disp_draw_buf_init(&draw, a, b, UI_W * BUF_LINES);
    lv_disp_drv_init(&driver);
    driver.hor_res = UI_W; driver.ver_res = UI_H;
    driver.flush_cb = flush_cb; driver.draw_buf = &draw;
    driver.antialiasing = 1;
    lcd_st7789_set_ready_cb(flush_ready);
    lv_disp_drv_register(&driver);
    ui_env_view_create(lv_scr_act());
    esp_timer_handle_t tick;
    const esp_timer_create_args_t args = {.callback = tick_cb, .name = "lv_tick"};
    if (esp_timer_create(&args, &tick) != ESP_OK || esp_timer_start_periodic(tick, 2000) != ESP_OK) {
        ESP_LOGE("UI_ENV", "LCD tick timer failed"); return;
    }
    if (xTaskCreate(ui_task, "ui_env", 4096, NULL, 3, NULL) != pdPASS)
        ESP_LOGE("UI_ENV", "LCD task allocation failed");
}
