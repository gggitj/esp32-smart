#include "power_guard.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lcd_st7789.h"
#include "pi_sensor_device.h"
#include "module_data_service.h"
#include "power_policy.h"

static volatile int source = -1; // -1 unknown, 0 battery, 1 external
static volatile bool low_warning;
const char *power_guard_source(void) { return source == 1 ? "external" : source == 0 ? "battery" : "unknown"; }
const char *power_guard_external_json(void) { return source < 0 ? "null" : source ? "true" : "false"; }
const char *power_guard_status(void) {
    if (source < 0) return "source_unknown_auto_sleep_inhibited";
    if (source == 1) return "external_supply_no_low_battery_shutdown";
    return low_warning ? "battery_low_warning" : "battery_guard_active";
}

static void power_guard_task(void *arg) {
    (void)arg;
    int pin = CONFIG_PLANTER_EXTERNAL_POWER_GPIO;
    // Fail closed for known occupied pins on this reference board.
    const int occupied[] = {0, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 17, 18, 19, 20, 21, 38, 39, 43, 44};
    for (unsigned i = 0; i < sizeof(occupied) / sizeof(occupied[0]); ++i)
        if (pin == occupied[i]) { ESP_LOGE("POWER_GUARD", "Detection GPIO conflicts with board wiring; source unknown"); pin = -1; break; }
    if (pin >= 0) {
        gpio_config_t cfg = {.pin_bit_mask = 1ULL << pin, .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE, .intr_type = GPIO_INTR_DISABLE};
        if (!GPIO_IS_VALID_GPIO(pin) || gpio_config(&cfg) != ESP_OK) pin = -1;
    }
    int candidate = -1; unsigned stable = 0;
    power_policy_t policy = {0};
    for (;;) {
        int reading = -1;
        if (pin >= 0) reading = gpio_get_level(pin) == CONFIG_PLANTER_EXTERNAL_POWER_HIGH;
#if CONFIG_PLANTER_BATTERY_ONLY_CONFIRMED
        else reading = 0;
#endif
        if (reading == candidate) { if (stable < 5) ++stable; }
        else { candidate = reading; stable = 1; }
        // External power immediately inhibits sleep; battery requires 500ms stability.
        source = reading == 1 ? 1 : stable >= 5 ? candidate : -1;
        pi_sensor_sample_t sample = {0};
        bool valid = pi_sensor_device_latest(&sample) && sample.battery_valid;
        bool fresh = valid && esp_timer_get_time() - sample.sample_time_us < 5000000;
        low_warning = fresh && sample.battery_volts < 3.50f;
        if (power_policy_update(&policy, source, fresh, sample.battery_volts, esp_timer_get_time())) {
            ESP_LOGW("POWER_GUARD", "Confirmed battery-only low voltage sustained 20s; closing Pi session");
            module_data_service_request_disconnect("low_battery", false);
            // Bounded grace, including absent Pi. Recheck supply before sleeping.
            vTaskDelay(pdMS_TO_TICKS(3000));
            bool external_now = pin >= 0 && (gpio_get_level(pin) == CONFIG_PLANTER_EXTERNAL_POWER_HIGH);
            pi_sensor_sample_t final_sample = {0};
            bool still_low = pi_sensor_device_latest(&final_sample) && final_sample.battery_valid &&
                esp_timer_get_time() - final_sample.sample_time_us < 5000000 && final_sample.battery_volts < 3.40f;
            if (external_now || !still_low) { policy.low_since_us = 0; continue; }
            lcd_st7789_backlight(false);
            esp_sleep_enable_timer_wakeup(1800ULL * 1000000ULL);
            esp_deep_sleep_start(); // Soft protection, not a physical load disconnect.
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
void power_guard_start(void) {
    if (xTaskCreate(power_guard_task, "power_guard", 4096, NULL, 2, NULL) != pdPASS)
        ESP_LOGE("POWER_GUARD", "Power guard task allocation failed");
}
