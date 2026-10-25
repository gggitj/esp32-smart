/**
 * @file diagnostics.c
 * @brief 项目通用诊断工具（I2C 扫描 + UI 触控日志）
 *
 * @details
 * 该模块聚焦“可观测性”：
 * - 提供开机 I2C 设备扫描
 * - 提供 UI 触控日志（带节流）
 * - 提供链路状态机迁移日志统一格式
 */

#include "diagnostics.h"

#include "board_pins.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"

void diagnostics_run_i2c_scan_touch_bus(const char *tag) {
    if (TP_I2C_SCL < 0 || TP_I2C_SDA < 0) {
        ESP_LOGW(tag, "I2C scan skipped: TP_I2C_SCL/TP_I2C_SDA not configured");
        return;
    }

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TP_I2C_PORT,
        .sda_io_num = TP_I2C_SDA,
        .scl_io_num = TP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err != ESP_OK) {
        ESP_LOGW(tag, "I2C scan init failed: %s", esp_err_to_name(err));
        return;
    }

    int found = 0;
    ESP_LOGI(tag, "I2C scan start (SCL=%d SDA=%d PORT=%d)", TP_I2C_SCL, TP_I2C_SDA, TP_I2C_PORT);
    for (uint8_t addr = 0x03; addr <= 0x77; ++addr) {
        if (i2c_master_probe(bus, addr, 10) == ESP_OK) {
            ESP_LOGI(tag, "I2C device found at 0x%02X", addr);
            found++;
        }
    }

    if (found == 0) {
        ESP_LOGW(tag, "I2C scan done: no device found");
    } else {
        ESP_LOGI(tag, "I2C scan done: %d device(s) found", found);
    }

    ESP_ERROR_CHECK(i2c_del_master_bus(bus));
}

/* 触控日志节流窗口，避免高频触点刷满串口。 */
void diagnostics_log_touch_sample(const char *tag, int16_t x, int16_t y, bool pressed, bool hit_disc) {
    static uint32_t s_touch_log_last_ms = 0;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

    if (!pressed || (now - s_touch_log_last_ms < 80U)) {
        return;
    }
    ESP_LOGI(tag, "touch x=%d y=%d pressed=%d hit_disc=%d", (int)x, (int)y, (int)pressed, (int)hit_disc);
    s_touch_log_last_ms = now;
}

void diagnostics_log_disconnect_click(const char *tag) {
    ESP_LOGI(tag, "disconnect button clicked");
}

void diagnostics_log_link_fsm(const char *tag,
                              const char *from_state,
                              const char *event,
                              const char *to_state,
                              const char *detail) {
    ESP_LOGI(tag,
             "[FSM] old=%s event=%s new=%s detail=%s",
             from_state ? from_state : "-",
             event ? event : "-",
             to_state ? to_state : "-",
             detail ? detail : "-");
}

