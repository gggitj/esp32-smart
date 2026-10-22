#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool temperature_valid, humidity_valid, pressure_valid, light_valid;
    bool battery_valid;
    float temperature_c, humidity_rh, pressure_hpa, light_lux;
    float battery_volts;
    int battery_percent;
    int64_t sample_time_us;
} pi_sensor_sample_t;

esp_err_t pi_sensor_device_start(void);
bool pi_sensor_device_latest(pi_sensor_sample_t *out);
