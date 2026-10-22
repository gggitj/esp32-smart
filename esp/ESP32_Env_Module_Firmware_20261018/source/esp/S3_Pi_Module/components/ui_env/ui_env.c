#include "ui_env.h"

#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_st7789.h"
#include "lvgl.h"
#include "pi_sensor_device.h"

#define UI_W 240
#define UI_H 240
#define BUF_LINES 16

/* Palette */
#define C_BG      0x07141C
#define C_PANEL   0x0E2431
#define C_LINE    0x1C3547
#define C_TITLE   0x7FD4C1
#define C_VALUE   0xFFFFFF
#define C_SUB     0xA8DADC
#define C_DIM     0x8FA9BB
#define C_ACCENT  0xD9B45A
#define C_GOOD    0x9BE564
#define C_WARN    0xFFB454
#define C_BAD     0xFF5A5A

static lv_disp_draw_buf_t s_draw;
static lv_color_t *s_buf1;
static lv_color_t *s_buf2;
static lv_disp_drv_t s_drv;

static lv_obj_t *s_temp;
static lv_obj_t *s_hum;
static lv_obj_t *s_pres;
static lv_obj_t *s_lux;
static lv_obj_t *s_batt;
static lv_obj_t *s_light_note;
static lv_obj_t *s_light_fill;
static lv_obj_t *s_link;

static void flush_ready(void)
{
    lv_disp_flush_ready(&s_drv);
}

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *px)
{
    (void)drv;
    lcd_st7789_flush(area->x1, area->y1, area->x2, area->y2, px);
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(2);
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                       int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void divider(lv_obj_t *parent, int y)
{
    lv_obj_t *l = lv_obj_create(parent);
    lv_obj_set_size(l, UI_W - 16, 1);
    lv_obj_set_pos(l, 8, y);
    lv_obj_set_style_bg_color(l, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_radius(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
}

/* Light buckets keep the same thresholds as the Pi dashboard. */
static const char *light_note(float lux, uint32_t *color)
{
    if (lux < 50)   { *color = C_BAD;   return "DARK - turn a light on"; }
    if (lux < 150)  { *color = C_WARN;  return "DIM  - hard to read"; }
    if (lux < 500)  { *color = C_GOOD;  return "GOOD - comfortable"; }
    if (lux < 1000) { *color = 0x82E3FF; return "BRIGHT"; }
    *color = 0xE0A3FF;
    return "TOO BRIGHT - avoid direct";
}

static void ui_task(void *arg)
{
    (void)arg;
    char buf[48];
    pi_sensor_sample_t s;
    TickType_t last_update = 0;

    for (;;) {
        /* LVGL is not thread safe: render and update in this one task. */
        lv_timer_handler();

        const TickType_t now = xTaskGetTickCount();
        if ((now - last_update) < pdMS_TO_TICKS(1000)) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        last_update = now;
        pi_sensor_device_latest(&s);

        if (s.temperature_valid) snprintf(buf, sizeof(buf), "%.1f C", s.temperature_c);
        else snprintf(buf, sizeof(buf), "--.- C");
        lv_label_set_text(s_temp, buf);

        if (s.humidity_valid) snprintf(buf, sizeof(buf), "%.1f %%RH", s.humidity_rh);
        else snprintf(buf, sizeof(buf), "--.- %%RH");
        lv_label_set_text(s_hum, buf);

        if (s.pressure_valid) snprintf(buf, sizeof(buf), "%.1f hPa", s.pressure_hpa);
        else snprintf(buf, sizeof(buf), "---- hPa");
        lv_label_set_text(s_pres, buf);

        uint32_t note_color = C_DIM;
        if (s.light_valid) {
            snprintf(buf, sizeof(buf), "%.0f lux", s.light_lux);
            lv_label_set_text(s_lux, buf);
            const char *note = light_note(s.light_lux, &note_color);
            lv_label_set_text(s_light_note, note);
            lv_obj_set_style_text_color(s_light_note, lv_color_hex(note_color), 0);

            float clamped = s.light_lux;
            if (clamped < 1) clamped = 1;
            if (clamped > 2000) clamped = 2000;
            /* log scale 1..2000 lux -> 0..100 % */
            float ratio = 0;
            for (float v = 1; v < clamped; v *= 1.15f) ratio += 1.0f / 55.0f;
            if (ratio > 1) ratio = 1;
            lv_obj_set_width(s_light_fill, (int)((UI_W - 24) * ratio));
            lv_obj_set_style_bg_color(s_light_fill, lv_color_hex(note_color), 0);
        } else {
            lv_label_set_text(s_lux, "--- lux");
            lv_label_set_text(s_light_note, "NO READING");
            lv_obj_set_style_text_color(s_light_note, lv_color_hex(C_DIM), 0);
            lv_obj_set_width(s_light_fill, 0);
        }

        if (s.battery_valid) {
            snprintf(buf, sizeof(buf), "%.2f V   %d%%", s.battery_volts, s.battery_percent);
            lv_obj_set_style_text_color(s_batt,
                lv_color_hex(s.battery_volts < 3.50f ? C_BAD : C_GOOD), 0);
        } else {
            snprintf(buf, sizeof(buf), "BAT --  (USB)");
            lv_obj_set_style_text_color(s_batt, lv_color_hex(C_DIM), 0);
        }
        lv_label_set_text(s_batt, buf);

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void ui_env_start(void)
{
    lv_init();

    s_buf1 = heap_caps_malloc(UI_W * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    s_buf2 = heap_caps_malloc(UI_W * BUF_LINES * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!s_buf1 || !s_buf2) return;
    lv_disp_draw_buf_init(&s_draw, s_buf1, s_buf2, UI_W * BUF_LINES);

    lv_disp_drv_init(&s_drv);
    s_drv.hor_res = UI_W;
    s_drv.ver_res = UI_H;
    s_drv.flush_cb = flush_cb;
    s_drv.draw_buf = &s_draw;
    lcd_st7789_set_ready_cb(flush_ready);
    lv_disp_drv_register(&s_drv);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(C_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(scr, 0, 0);

    /* Header */
    label(scr, &lv_font_montserrat_14, C_TITLE, 8, 5, "ENV MONITOR");
    s_link = label(scr, &lv_font_montserrat_14, C_DIM, 168, 5, "USB");
    divider(scr, 25);

    /* Temperature - the primary number */
    s_temp = label(scr, &lv_font_montserrat_32, C_VALUE, 8, 31, "--.- C");
    s_hum  = label(scr, &lv_font_montserrat_24, C_SUB, 8, 75, "--.- %RH");

    divider(scr, 110);

    /* Pressure */
    label(scr, &lv_font_montserrat_14, C_DIM, 8, 118, "PRESSURE");
    s_pres = label(scr, &lv_font_montserrat_14, C_VALUE, 92, 118, "---- hPa");

    /* Light */
    label(scr, &lv_font_montserrat_14, C_DIM, 8, 138, "LIGHT");
    s_lux = label(scr, &lv_font_montserrat_14, C_ACCENT, 92, 138, "--- lux");

    lv_obj_t *bar = lv_obj_create(scr);
    lv_obj_set_size(bar, UI_W - 24, 7);
    lv_obj_set_pos(bar, 8, 158);
    lv_obj_set_style_bg_color(bar, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 4, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);

    s_light_fill = lv_obj_create(scr);
    lv_obj_set_size(s_light_fill, 0, 7);
    lv_obj_set_pos(s_light_fill, 8, 158);
    lv_obj_set_style_bg_color(s_light_fill, lv_color_hex(C_GOOD), 0);
    lv_obj_set_style_bg_opa(s_light_fill, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_light_fill, 0, 0);
    lv_obj_set_style_radius(s_light_fill, 4, 0);
    lv_obj_set_style_pad_all(s_light_fill, 0, 0);

    s_light_note = label(scr, &lv_font_montserrat_14, C_DIM, 8, 170, "NO READING");

    divider(scr, 192);

    /* Battery */
    label(scr, &lv_font_montserrat_14, C_DIM, 8, 200, "POWER");
    s_batt = label(scr, &lv_font_montserrat_14, C_GOOD, 66, 200, "BAT --  (USB)");

    const esp_timer_create_args_t tick_args = {
        .callback = tick_cb,
        .name = "lv_tick",
    };
    esp_timer_handle_t tick;
    esp_timer_create(&tick_args, &tick);
    esp_timer_start_periodic(tick, 2000);

    xTaskCreate(ui_task, "ui_env", 4096, NULL, 3, NULL);
}
