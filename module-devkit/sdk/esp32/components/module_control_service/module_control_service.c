#include "module_control_service.h"
#include "linkit.h"
#include "linkit_pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define CONFIG_COMMAND 0x03u
#define KEEPALIVE_COMMAND 0x7eu
#define HEARTBEAT_MS 1000u
#define CONFIG_INTERVAL_MS 1000u
static TaskHandle_t task_handle;
static module_control_service_config_t service_config;
static char config_json[192];

static esp_err_t configure_det(void) {
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << POGO_PIN_DET, .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) return err;
    return gpio_set_level(POGO_PIN_DET, 0);
}

static void control_task(void *arg) {
    (void)arg;
    TickType_t last_config = 0, last_heartbeat = 0;
    linkit_rx_frame_t rx;
    for (;;) {
        linkit_poll(0);
        while (linkit_read_frame(&rx, 0) == ESP_OK) {
            // Applications may add a callback in a future SDK revision. The
            // control service deliberately consumes only platform frames.
        }
        TickType_t now = xTaskGetTickCount();
        if (!last_config || now - last_config >= pdMS_TO_TICKS(CONFIG_INTERVAL_MS)) {
            linkit_frame_t frame = {
                .module_id = service_config.module_id,
                .msg_type = LINKIT_MSG_STREAM, .flags = LINKIT_FLAG_NONE,
                .cmd_id = CONFIG_COMMAND, .seq = 0,
                .payload = (const uint8_t *)config_json,
                .payload_len = (uint16_t)strlen(config_json),
            };
            linkit_write_frame(&frame, 0);
            last_config = now;
        }
        if (linkit_get_state() == LINKIT_STATE_WIRED &&
            (!last_heartbeat || now - last_heartbeat >= pdMS_TO_TICKS(HEARTBEAT_MS))) {
            const linkit_frame_t frame = {
                .module_id = service_config.module_id,
                .msg_type = LINKIT_MSG_STREAM, .flags = LINKIT_FLAG_NONE,
                .cmd_id = KEEPALIVE_COMMAND, .seq = 0, .payload = NULL, .payload_len = 0,
            };
            linkit_write_frame(&frame, 0);
            last_heartbeat = now;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t module_control_service_start(const module_control_service_config_t *config) {
    if (!config || !config->transport_mode || !config->package_transport || !config->module_type)
        return ESP_ERR_INVALID_ARG;
    if (strcmp(config->transport_mode, "fixed_ble") != 0 &&
        strcmp(config->transport_mode, "fixed_hotspot_lan") != 0) return ESP_ERR_INVALID_ARG;
    if (strcmp(config->transport_mode, "fixed_hotspot_lan") == 0 &&
        strcmp(config->package_transport, "data") != 0) return ESP_ERR_INVALID_ARG;
    service_config = *config;
    int count = snprintf(config_json, sizeof(config_json),
        "{\"schema\":\"module-transport-v1\",\"transport_mode\":\"%s\",\"package_transport\":\"%s\"}",
        config->transport_mode, config->package_transport);
    if (count < 0 || (size_t)count >= sizeof(config_json)) return ESP_ERR_INVALID_SIZE;
    esp_err_t err = configure_det();
    if (err != ESP_OK) return err;
    linkit_device_config_t link_config = LINKIT_DEVICE_CONFIG_DEFAULT();
    static linkit_ext_desc_t descriptor;
    descriptor = (linkit_ext_desc_t){
        .ext_version = 1, .module_id = config->module_id,
        .frame_proto_min = 0x0201, .frame_proto_max = 0x0201,
        .mtu_uart = LINKIT_FRAME_MAX_PAYLOAD, .mtu_espnow = 0,
        .uart_high_baud = 2000000, .pin_mux_caps = LINKIT_MUX_UART_DEFAULT,
        .feature_caps = LINKIT_FC_FRAME_V21 | LINKIT_FC_STREAM,
    };
    link_config.device_ext_desc = &descriptor;
    err = linkit_device_install(&link_config);
    if (err != ESP_OK) return err;
    if (xTaskCreate(control_task, "module_control", 4096, NULL, 8, &task_handle) != pdPASS) {
        linkit_uninstall(); return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void module_control_service_stop(void) {
    if (task_handle) { vTaskDelete(task_handle); task_handle = NULL; }
    linkit_uninstall();
}
