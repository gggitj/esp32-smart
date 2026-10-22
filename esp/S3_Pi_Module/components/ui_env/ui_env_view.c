#include "ui_env_view.h"
#include <math.h>
#include <stdio.h>

LV_FONT_DECLARE(plant_zh_12);
LV_FONT_DECLARE(plant_zh_14);
LV_FONT_DECLARE(plant_roboto_24);
LV_FONT_DECLARE(plant_roboto_36);

#define FG 0xFFFFFF
#define SECONDARY 0xBDBDBD
#define OUTLINE 0x303030
#define PRIMARY 0xA8D5A2

static lv_obj_t *temperature, *humidity, *pressure, *light, *battery, *power, *light_note, *light_fill;

static lv_obj_t *text(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                      int x, int y, int w, int h, const char *value, lv_text_align_t align)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_remove_style_all(obj); // Do not inherit a theme's colored label state.
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_text_align(obj, align, 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_label_set_text(obj, value);
    return obj;
}

static lv_obj_t *shape(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color, int radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y); lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    return obj;
}

void ui_env_view_create(lv_obj_t *screen)
{
    lv_obj_remove_style_all(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(screen, LV_SCROLLBAR_MODE_OFF);
    // Compact Material hierarchy: 16px margins, 4/8px spacing, neutral text,
    // display-small 36px, headline-small 24px, title-small 14px, body-small 12px.
    text(screen, &plant_zh_14, FG, 16, 12, 116, 22, "植物环境监测", LV_TEXT_ALIGN_LEFT);
    power = text(screen, &plant_zh_12, SECONDARY, 140, 14, 84, 18, "供电待检测", LV_TEXT_ALIGN_RIGHT);
    shape(screen, 16, 36, 208, 1, OUTLINE, 0);

    text(screen, &plant_zh_12, SECONDARY, 16, 46, 124, 18, "空气温度", LV_TEXT_ALIGN_LEFT);
    temperature = text(screen, &plant_roboto_36, FG, 16, 66, 116, 45, "--.-", LV_TEXT_ALIGN_LEFT);
    text(screen, &plant_zh_14, SECONDARY, 125, 85, 26, 22, "\xC2\xB0" "C", LV_TEXT_ALIGN_LEFT);
    text(screen, &plant_zh_12, SECONDARY, 164, 46, 60, 18, "空气湿度", LV_TEXT_ALIGN_RIGHT);
    humidity = text(screen, &plant_roboto_24, FG, 160, 72, 64, 30, "--.-", LV_TEXT_ALIGN_RIGHT);
    text(screen, &plant_zh_12, SECONDARY, 164, 100, 60, 18, "%", LV_TEXT_ALIGN_RIGHT);

    shape(screen, 16, 120, 208, 1, OUTLINE, 0);
    text(screen, &plant_zh_12, SECONDARY, 16, 131, 88, 18, "大气压力", LV_TEXT_ALIGN_LEFT);
    pressure = text(screen, &plant_zh_14, FG, 104, 128, 120, 22, "---- hPa", LV_TEXT_ALIGN_RIGHT);
    text(screen, &plant_zh_12, SECONDARY, 16, 157, 88, 18, "环境光照", LV_TEXT_ALIGN_LEFT);
    light = text(screen, &plant_zh_14, FG, 104, 154, 120, 22, "--- lux", LV_TEXT_ALIGN_RIGHT);
    shape(screen, 16, 181, 208, 4, OUTLINE, 2);
    light_fill = shape(screen, 16, 181, 0, 4, PRIMARY, 2);
    light_note = text(screen, &plant_zh_12, SECONDARY, 16, 190, 208, 18, "等待光照数据", LV_TEXT_ALIGN_LEFT);

    shape(screen, 16, 214, 208, 1, OUTLINE, 0);
    text(screen, &plant_zh_12, SECONDARY, 16, 220, 48, 18, "电池", LV_TEXT_ALIGN_LEFT);
    battery = text(screen, &plant_zh_14, FG, 66, 217, 158, 22, "--%   --.-- V", LV_TEXT_ALIGN_RIGHT);
}

void ui_env_view_update(const ui_env_snapshot_t *s)
{
    char buf[40];
    if (s->temperature_valid && isfinite(s->temperature)) snprintf(buf, sizeof(buf), "%.1f", s->temperature);
    else snprintf(buf, sizeof(buf), "--.-");
    lv_label_set_text(temperature, buf);
    if (s->humidity_valid && isfinite(s->humidity)) snprintf(buf, sizeof(buf), "%.1f", s->humidity);
    else snprintf(buf, sizeof(buf), "--.-");
    lv_label_set_text(humidity, buf);
    if (s->pressure_valid && isfinite(s->pressure)) snprintf(buf, sizeof(buf), "%.1f hPa", s->pressure);
    else snprintf(buf, sizeof(buf), "---- hPa");
    lv_label_set_text(pressure, buf);
    if (s->light_valid && isfinite(s->light) && s->light >= 0) {
        snprintf(buf, sizeof(buf), "%.0f lux", s->light);
        lv_label_set_text(light, buf);
        const char *note = s->light < 50 ? "光线偏暗，请按植物需求补光" :
            s->light < 150 ? "光线较弱，请按植物需求调整" :
            s->light < 1000 ? "散射光环境，请按植物品种调整" : "光线较强，注意避免暴晒";
        lv_label_set_text(light_note, note);
        float ratio = log1pf(fminf(s->light, 2000)) / log1pf(2000);
        lv_obj_set_width(light_fill, (int)(208 * ratio));
    } else {
        lv_label_set_text(light, "--- lux");
        lv_label_set_text(light_note, "光照传感器暂无数据");
        lv_obj_set_width(light_fill, 0);
    }
    if (s->battery_valid && isfinite(s->battery_volts)) {
        int pct = s->battery_percent < 0 ? 0 : s->battery_percent > 100 ? 100 : s->battery_percent;
        snprintf(buf, sizeof(buf), "%s%d%%   %.2f V", s->battery_volts < 3.50f ? "低电 " : "", pct, s->battery_volts);
    } else snprintf(buf, sizeof(buf), "--%%   --.-- V");
    lv_label_set_text(battery, buf);
    // Warning uses an explicit LOW word, not color alone. Keep the light bar
    // semantically independent of battery state and all glyphs neutral.
    lv_label_set_text(power, s->power_source == 1 ? "外部供电" : s->power_source == 0 ? "电池供电" : "供电待检测");
}
