#include "ui_env_view.h"
#include "lcd_rgb565.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static uint16_t frame[240 * 240];
static lv_color_t buffer[240 * 16];
static void flush(lv_disp_drv_t *d, const lv_area_t *a, lv_color_t *p)
{
    for (int y = a->y1; y <= a->y2; ++y)
        for (int x = a->x1; x <= a->x2; ++x) frame[y * 240 + x] = (p++)->full;
    lv_disp_flush_ready(d);
}
static void check_layout(lv_obj_t *root)
{
    lv_obj_update_layout(root);
    CHECK(lv_obj_get_style_bg_color(root, 0).full == 0);
    for (unsigned i = 0; i < lv_obj_get_child_cnt(root); ++i) {
        lv_obj_t *child = lv_obj_get_child(root, i);
        CHECK(lv_obj_get_x(child) >= 0 && lv_obj_get_y(child) >= 0);
        CHECK(lv_obj_get_x(child) + lv_obj_get_width(child) <= 240);
        CHECK(lv_obj_get_y(child) + lv_obj_get_height(child) <= 240);
        if (lv_obj_check_type(child, &lv_label_class)) {
            lv_point_t size;
            lv_txt_get_size(&size, lv_label_get_text(child), lv_obj_get_style_text_font(child, 0),
                            0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
            CHECK(size.x <= lv_obj_get_content_width(child));
            CHECK(size.y <= lv_obj_get_content_height(child));
            const char *value = lv_label_get_text(child);
            const lv_font_t *font = lv_obj_get_style_text_font(child, 0);
            for (uint32_t pos = 0; value[pos]; ) {
                uint32_t cp = _lv_txt_encoded_next(value, &pos);
                lv_font_glyph_dsc_t glyph;
                if (!lv_font_get_glyph_dsc(font, &glyph, cp, 0)) {
                    fprintf(stderr, "Missing U+%04X in label %s\n", (unsigned)cp, value);
                    CHECK(false);
                }
            }
            // Reversed gray bytes used to create green/purple glyph fringes.
            uint16_t color = lv_obj_get_style_text_color(child, 0).full;
            CHECK(color == lv_color_hex(0xffffff).full || color == lv_color_hex(0xbdbdbd).full);
        }
    }
}
static void image(const char *name)
{
    lv_refr_now(NULL);
    for (int y = 0; y < 240; ++y) { CHECK(frame[y * 240] == 0); CHECK(frame[y * 240 + 239] == 0); }
    for (int y = 64; y < 112; ++y) {
        for (int x = 16; x < 132; ++x) {
            uint16_t v = frame[y * 240 + x];
            int r = ((v >> 11) & 31) * 255 / 31, g = ((v >> 5) & 63) * 255 / 63, b = (v & 31) * 255 / 31;
            // RGB565 quantization allows a few gray-level units, not colored fringes.
            CHECK(abs(r - g) <= 8 && abs(g - b) <= 8);
        }
    }
    FILE *f = fopen(name, "wb"); CHECK(f);
    fprintf(f, "P6\n240 240\n255\n");
    for (unsigned i = 0; i < 240 * 240; ++i) {
        uint16_t v = frame[i];
        unsigned char rgb[] = {((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31};
        CHECK(fwrite(rgb, 1, 3, f) == 3);
    }
    fclose(f);
}
int main(void)
{
    uint16_t pixels[] = {0x0000, 0xffff, 0xf800, 0x07e0, 0x001f, 0x8410};
    uint8_t bytes[12]; lcd_rgb565_pack_be(bytes, pixels, 6);
    const uint8_t expected[] = {0,0,255,255,248,0,7,224,0,31,132,16};
    CHECK(memcmp(bytes, expected, sizeof(bytes)) == 0);
    lv_init();
    static lv_disp_draw_buf_t draw;
    static lv_disp_drv_t d;
    lv_disp_draw_buf_init(&draw, buffer, NULL, 240 * 16);
    lv_disp_drv_init(&d); d.hor_res = d.ver_res = 240; d.draw_buf = &draw; d.flush_cb = flush;
    lv_disp_drv_register(&d);
    ui_env_view_create(lv_scr_act());
    ui_env_snapshot_t data = {0}; data.power_source = -1;
    ui_env_view_update(&data); check_layout(lv_scr_act()); image("lcd-missing.ppm");
    data = (ui_env_snapshot_t){true,true,true,true,true,25.6f,60.8f,1015.5f,700,3.82f,45,-1};
    ui_env_view_update(&data); check_layout(lv_scr_act()); image("lcd-normal.ppm");
    data.temperature = -40; data.humidity = 100; data.pressure = 1100; data.light = 65535;
    data.battery_volts = 3.3f; data.battery_percent = 100; data.power_source = 1;
    ui_env_view_update(&data); check_layout(lv_scr_act()); image("lcd-extreme.ppm");
    puts("PASS: black background, neutral glyphs, Simplified Chinese glyph coverage, layout bounds, RGB565 wire bytes");
}
