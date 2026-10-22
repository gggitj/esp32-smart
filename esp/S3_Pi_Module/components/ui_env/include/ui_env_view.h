#pragma once
#include "lvgl.h"
#include <stdbool.h>

typedef struct {
    bool temperature_valid, humidity_valid, pressure_valid, light_valid, battery_valid;
    float temperature, humidity, pressure, light, battery_volts;
    int battery_percent;
    int power_source; // -1 unknown, 0 battery, 1 external
} ui_env_snapshot_t;

// Pure LVGL view, shared by firmware and the desktop pixel/layout regression.
void ui_env_view_create(lv_obj_t *screen);
void ui_env_view_update(const ui_env_snapshot_t *snapshot);
