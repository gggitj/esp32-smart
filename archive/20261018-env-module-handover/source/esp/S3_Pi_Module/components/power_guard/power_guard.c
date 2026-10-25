#include "power_guard.h"

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_st7789.h"
#include "pi_sensor_device.h"

#define TAG "POWER_GUARD"

/* 1S Li-ion. Tune here. */
#define WARN_VOLTS        3.50f
#define SHUTDOWN_VOLTS    3.40f
#define SAMPLE_MS         5000
#define SHUTDOWN_SAMPLES  4        /* 4 x 5 s = 20 s below threshold */
#define SLEEP_WAKE_S      1800ULL  /* re-check every 30 min while hibernating */

static void hibernate(float volts, const char *why)
{
    ESP_LOGE(TAG, "%s: battery %.2f V -> shutting down", why, volts);
    lcd_st7789_backlight(false);
    vTaskDelay(pdMS_TO_TICKS(2000)); /* let the log flush */
    esp_sleep_enable_timer_wakeup(SLEEP_WAKE_S * 1000000ULL);
    esp_deep_sleep_start();
}

static void power_guard_task(void *arg)
{
    (void)arg;
    pi_sensor_sample_t s;
    unsigned low_samples = 0;
    bool warned = false;

    /* Give the sensor task time to produce the first reading. */
    vTaskDelay(pdMS_TO_TICKS(3000));
    if (pi_sensor_device_latest(&s) && s.battery_valid && s.battery_volts < SHUTDOWN_VOLTS) {
        hibernate(s.battery_volts, "boot check");
    }

    for (;;) {
        if (!pi_sensor_device_latest(&s) || !s.battery_valid) {
            /* No cell fitted (USB powered) - nothing to protect. */
            low_samples = 0;
            warned = false;
            vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
            continue;
        }

        if (s.battery_volts < WARN_VOLTS && !warned) {
            ESP_LOGW(TAG, "low battery warning: %.2f V", s.battery_volts);
            warned = true;
        } else if (s.battery_volts >= (WARN_VOLTS + 0.05f)) {
            warned = false;
        }

        if (s.battery_volts < SHUTDOWN_VOLTS) {
            ++low_samples;
            ESP_LOGW(TAG, "battery %.2f V below %.2f V (%u/%u)",
                     s.battery_volts, SHUTDOWN_VOLTS, low_samples, SHUTDOWN_SAMPLES);
        } else {
            low_samples = 0;
        }

        if (low_samples >= SHUTDOWN_SAMPLES) {
            hibernate(s.battery_volts, "sustained low voltage");
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_MS));
    }
}

void power_guard_start(void)
{
    ESP_LOGI(TAG, "active: warn %.2f V, shutdown %.2f V for %u s, wake every %llu s",
             WARN_VOLTS, SHUTDOWN_VOLTS,
             (unsigned)(SHUTDOWN_SAMPLES * SAMPLE_MS / 1000),
             (unsigned long long)SLEEP_WAKE_S);
    xTaskCreate(power_guard_task, "power_guard", 4096, NULL, 2, NULL);
}
