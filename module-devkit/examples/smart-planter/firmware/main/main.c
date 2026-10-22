#include "esp_check.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "linkit.h"
#include "linkit_pins.h"
#include "module_data_service.h"
#include "pi_sensor_device.h"
#include "lcd_st7789.h"
#include "ui_env.h"
#include "power_guard.h"
#include "cJSON.h"
#include "mbedtls/base64.h"

#define LINKIT_TASK_STACK_BYTES 8192U
#define LINKIT_TASK_PRIORITY    8U

#define POGO_MODULE_ID        0x50U /* module type reported in the LNK1 handshake */
#define POGO_CMD_KEEPALIVE    0x7EU
#define POGO_HEARTBEAT_MS     1000U
#define POGO_CMD_MODULE_CONFIG 0x03U
#define POGO_MODULE_CONFIG_INTERVAL_MS 1000U

static const char *TAG = "POGO_DEVICE";

#if CONFIG_APP_PI_TRANSPORT_BLE
#define MODULE_TRANSPORT_MODE "fixed_ble"
#else
#define MODULE_TRANSPORT_MODE "fixed_hotspot_lan"
#endif
#if CONFIG_APP_PI_PACKAGE_HOTSPOT_HTTP
#define MODULE_PACKAGE_TRANSPORT "hotspot_lan"
#else
#define MODULE_PACKAGE_TRANSPORT "data"
#endif
static const char s_module_transport_config[] =
    "{\"schema\":\"module-transport-v1\",\"transport_mode\":\"" MODULE_TRANSPORT_MODE
    "\",\"package_transport\":\"" MODULE_PACKAGE_TRANSPORT "\"}";
static bool s_transport_config_logged;
static bool s_transport_config_attempt_logged;

/* Generated signed host plugin package is embedded in the ESP application
 * image and served through the documented BLE package_get/package_chunk API. */
extern const uint8_t _binary_embedded_package_json_start[];
extern const uint8_t _binary_embedded_package_json_end[];
extern const uint8_t _binary_embedded_package_tar_zst_start[];
extern const uint8_t _binary_embedded_package_tar_zst_end[];

static size_t embedded_package_read(const char *file, uint32_t offset,
                                    uint8_t *buffer, size_t capacity,
                                    uint32_t *total, void *context)
{
    (void)context;
    if (!file || !buffer || !total || !capacity) return 0;
    const uint8_t *begin = NULL;
    const uint8_t *end = NULL;
    if (strcmp(file, "package.json") == 0) {
        begin = _binary_embedded_package_json_start;
        end = _binary_embedded_package_json_end;
    } else if (strcmp(file, "package.tar.zst") == 0) {
        begin = _binary_embedded_package_tar_zst_start;
        end = _binary_embedded_package_tar_zst_end;
    } else {
        return 0;
    }
    const size_t length = (size_t)(end - begin);
    if (!length || length > UINT32_MAX) return 0;
    *total = (uint32_t)length;
    if (offset >= length || !buffer || !capacity) return 0;
    size_t count = length - offset;
    if (count > capacity) count = capacity;
    memcpy(buffer, begin + offset, count);
    return count;
}

#if CONFIG_APP_PI_DATA_ENABLE
static const module_data_wifi_config_t s_wifi_config = {
    .ssid = CONFIG_APP_PI_DATA_SSID,
    .password = CONFIG_APP_PI_DATA_PASSWORD,
    .max_retry = 10,
    .static_ip = CONFIG_APP_PI_DATA_STATIC_IP,
    .netmask = CONFIG_APP_PI_DATA_NETMASK,
    .gateway = CONFIG_APP_PI_DATA_GATEWAY,
};
static module_data_config_t s_pi_data_config;
static bool s_module_data_service_started;
#if CONFIG_APP_PI_TRANSPORT_BLE
static bool s_ble_data_service_started;
#endif
static bool s_package_http_started;

static bool pi_command_callback(const char *event, const char *json, void *context)
{
    (void)context;
    ESP_LOGI(TAG, "Orange Pi command event=%s payload=%s", event ? event : "", json ? json : "");
    if (event && strcmp(event, "data") == 0 && json) {
        cJSON *outer = cJSON_Parse(json);
        const cJSON *encoded = outer ? cJSON_GetObjectItemCaseSensitive(outer, "payload") : NULL;
        ESP_LOGI(TAG, "terminal data command envelope parsed=%d payload_string=%d",
                 outer != NULL, cJSON_IsString(encoded));
        if (cJSON_IsString(encoded)) {
            uint8_t decoded[256]; size_t decoded_length = 0;
            const int decode_result = mbedtls_base64_decode(decoded, sizeof(decoded), &decoded_length,
                                      (const unsigned char *)encoded->valuestring,
                                      strlen(encoded->valuestring));
            ESP_LOGI(TAG, "terminal data command base64 result=%d decoded_bytes=%u",
                     decode_result, (unsigned)decoded_length);
            if (decode_result == 0) {
                cJSON *command = cJSON_ParseWithLength((const char *)decoded, decoded_length);
                const cJSON *name = command ? cJSON_GetObjectItemCaseSensitive(command, "command") : NULL;
                ESP_LOGI(TAG, "terminal data command JSON parsed=%d name=%s",
                         command != NULL, cJSON_IsString(name) ? name->valuestring : "<missing>");
                if (cJSON_IsString(name) && strcmp(name->valuestring, "request_disconnect") == 0) {
                    ESP_LOGI(TAG, "terminal request_disconnect command recognized");
                    const esp_err_t result = module_data_service_request_disconnect("maintenance", false);
                    ESP_LOGI(TAG, "terminal requested module disconnect: %s", esp_err_to_name(result));
                    cJSON_Delete(command); cJSON_Delete(outer);
                    return result == ESP_OK;
                }
                cJSON_Delete(command);
            }
        }
        cJSON_Delete(outer);
    }
    return event && (!strcmp(event, "start") || !strcmp(event, "stop") || !strcmp(event, "configure") || !strcmp(event, "data"));
}

static void pi_data_task(void *arg)
{
    (void)arg;
    uint32_t count = 0;
    while (true) {
        pi_sensor_sample_t sample = {0};
        pi_sensor_device_latest(&sample);
        char temperature[24] = "null", humidity[24] = "null";
        char pressure[24] = "null", light[24] = "null";
        char battery_v[24] = "null", battery_pct[24] = "null";
        if (sample.temperature_valid) snprintf(temperature, sizeof(temperature), "%.2f", sample.temperature_c);
        if (sample.humidity_valid) snprintf(humidity, sizeof(humidity), "%.2f", sample.humidity_rh);
        if (sample.pressure_valid) snprintf(pressure, sizeof(pressure), "%.2f", sample.pressure_hpa);
        if (sample.light_valid) snprintf(light, sizeof(light), "%.1f", sample.light_lux);
        if (sample.battery_valid) {
            snprintf(battery_v, sizeof(battery_v), "%.2f", sample.battery_volts);
            snprintf(battery_pct, sizeof(battery_pct), "%d", sample.battery_percent);
        }
        char payload[768];
        const int length = snprintf(payload, sizeof(payload),
                                    "{\"source\":\"s3_sensor_module\",\"heartbeat\":%lu,\"linkit\":\"wired-compatible\","
                                    "\"temperature_C\":%s,\"humidity_RH\":%s,\"pressure_hPa\":%s,\"light_lux\":%s,"
                                     "\"battery_V\":%s,\"battery_pct\":%s,"
                                     "\"power_source\":\"%s\",\"external_power\":%s,\"charging\":null,"
                                     "\"power_protection\":\"%s\"}",
                                    (unsigned long)count++, temperature, humidity, pressure, light,
                                     battery_v, battery_pct, power_guard_source(), power_guard_external_json(), power_guard_status());
        if (length <= 0 || (size_t)length >= sizeof(payload)) { vTaskDelay(pdMS_TO_TICKS(2000)); continue; }
        ESP_LOGI(TAG, "sensor payload: %s", payload);
        const esp_err_t err = module_data_service_publish((const uint8_t *)payload, (size_t)length);
        if (err == ESP_OK) ESP_LOGI(TAG, "Orange Pi test data published");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

static void disconnect_button_task(void *arg)
{
    (void)arg;
    const gpio_config_t button = {
        .pin_bit_mask = 1ULL << BOOT_BTN_PIN,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&button) != ESP_OK) { vTaskDelete(NULL); return; }
    unsigned held_ticks = 0;
    bool issued = false;
    ESP_LOGI(TAG, "module disconnect button GPIO%d ready: hold 1 second", BOOT_BTN_PIN);
    for (;;) {
        if (gpio_get_level(BOOT_BTN_PIN) == 0) {
            if (held_ticks < 50) ++held_ticks;
            if (held_ticks >= 50 && !issued) {
                issued = true;
                const esp_err_t result = module_data_service_request_disconnect("user_button", false);
                ESP_LOGI(TAG, "local module button disconnect result=%s", esp_err_to_name(result));
            }
        } else { held_ticks = 0; issued = false; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void wifi_recovery_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        if (!s_module_data_service_started) {
            ESP_LOGI(TAG, "retry module data service initialization");
            const esp_err_t service_err = module_data_service_start(&s_pi_data_config, pi_command_callback, NULL);
            if (service_err == ESP_OK) {
                s_module_data_service_started = true;
#if CONFIG_APP_PI_TRANSPORT_BLE
                const esp_err_t ble_err = module_data_service_start_ble(CONFIG_APP_PI_DATA_BLE_NAME);
                s_ble_data_service_started = ble_err == ESP_OK;
                if (ble_err != ESP_OK)
                    ESP_LOGW(TAG, "BLE data service retry failed: %s; sensor and LinkIt remain active",
                              esp_err_to_name(ble_err));
#endif
            } else {
                ESP_LOGW(TAG, "module data service retry failed: %s; sensor and LinkIt remain active",
                         esp_err_to_name(service_err));
            }
        }
#if CONFIG_APP_PI_TRANSPORT_BLE
        if (s_module_data_service_started && !s_ble_data_service_started) {
            const esp_err_t ble_err = module_data_service_start_ble(CONFIG_APP_PI_DATA_BLE_NAME);
            if (ble_err == ESP_OK) s_ble_data_service_started = true;
            else ESP_LOGW(TAG, "BLE data service retry failed: %s", esp_err_to_name(ble_err));
        }
#endif

        // Do not attempt to create a socket or station until lwIP/netif is
        // initialized. If the MDS service itself did not start, keep sensors
        // and LinkIt alive and retry it first.
        if (!s_module_data_service_started) continue;

        if (s_module_data_service_started && !s_package_http_started) {
            ESP_LOGI(TAG, "retry WiFi station connection to configured hotspot");
            const esp_err_t wifi_err = module_data_wifi_start(&s_wifi_config);
            if (wifi_err == ESP_OK) {
#if CONFIG_APP_PI_PACKAGE_HOTSPOT_HTTP
                const esp_err_t http_err = module_data_service_start_package_http(
                    (uint16_t)(CONFIG_APP_PI_DATA_PORT + 1), CONFIG_APP_PI_DATA_GATEWAY);
                if (http_err == ESP_OK) s_package_http_started = true;
                else {
                    ESP_LOGW(TAG, "HTTP package endpoint retry failed: %s; BLE/LinkIt remain active",
                             esp_err_to_name(http_err));
                    module_data_wifi_stop();
                }
#else
                // LAN packages and business data share the authenticated TCP
                // session. No BLE stack or extra HTTP server is needed.
                s_package_http_started = true;
                ESP_LOGI(TAG, "Pi LAN ready: authenticated TCP carries data and package");
#endif
            } else {
                ESP_LOGW(TAG, "WiFi station retry failed: %s; BLE/LinkIt/sensors remain active",
                         esp_err_to_name(wifi_err));
            }
        }

        if (s_module_data_service_started && s_package_http_started
#if CONFIG_APP_PI_TRANSPORT_BLE
            && s_ble_data_service_started
#endif
            )
            vTaskDelete(NULL);
    }
}
#endif

static const linkit_ext_desc_t s_device_desc = {
    .ext_version    = 1,
    .module_id      = POGO_MODULE_ID,
    .frame_proto_min = 0x0201,
    .frame_proto_max = 0x0201,
    .mtu_uart       = LINKIT_FRAME_MAX_PAYLOAD,
    .mtu_espnow     = 0,
    .uart_high_baud = 2000000,
    .pin_mux_caps   = LINKIT_MUX_UART_DEFAULT,
    .feature_caps   = LINKIT_FC_FRAME_V21 | LINKIT_FC_STREAM,
};

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS failed");
        err = nvs_flash_init();
    }
    return err;
}

/* Module side drives DET low so the host's pulled-up DET reads "attached". */
static esp_err_t init_device_det(void)
{
    const gpio_config_t config = {
        .pin_bit_mask  = 1ULL << POGO_PIN_DET,
        .mode          = GPIO_MODE_OUTPUT,
        .pull_up_en    = GPIO_PULLUP_DISABLE,
        .pull_down_en  = GPIO_PULLDOWN_DISABLE,
        .intr_type     = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&config), TAG, "configure DET failed");
    // A firmware reboot must release the previous physical session before
    // announcing this instance again. Periodic config frames intentionally
    // cannot override a host-side closed session without a detach edge.
    ESP_RETURN_ON_ERROR(gpio_set_level(POGO_PIN_DET, 1), TAG, "release DET failed");
    vTaskDelay(pdMS_TO_TICKS(250));
    return gpio_set_level(POGO_PIN_DET, 0);
}

static const char *flow_state_label(linkit_flow_state_t s)
{
    switch (s) {
    case LINKIT_FLOW_S0_DISCONNECTED:        return "S0_DISCONNECTED";
    case LINKIT_FLOW_S1_ATTACH_DETECT:       return "S1_ATTACH_DETECT";
    case LINKIT_FLOW_S2_LOW_SPEED_HANDSHAKE: return "S2_HANDSHAKING";
    case LINKIT_FLOW_S3_HIGH_SPEED_SWITCH:   return "S3_HIGH_SPEED";
    case LINKIT_FLOW_S4_DUAL_MODE_RUNTIME:   return "S4_WIRED";
    case LINKIT_FLOW_S5_LINK_RECOVERY:       return "S5_RECOVERY";
    default:                                 return "UNKNOWN";
    }
}

static void linkit_task(void *arg)
{
    (void)arg;
    linkit_flow_state_t last_wired = (linkit_flow_state_t)-1;
    bool logged_wired = false;
    TickType_t last_log_tick = 0;
    TickType_t last_hb_tick = 0;
    TickType_t last_config_tick = 0;
    bool config_send_failure_logged = false;
    bool config_task_logged = false;
    linkit_rx_frame_t rx;

    ESP_LOGI(TAG, "LinkIt device pump started, waiting for WHO?...");

    while (true) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(linkit_poll(0));

        while (linkit_read_frame(&rx, 0) == ESP_OK) {
            ESP_LOGI(TAG, "rx frame module=0x%02X msg=%u flags=0x%02X cmd=0x%02X len=%u",
                     rx.frame.module_id, rx.frame.msg_type, rx.frame.flags,
                     rx.frame.cmd_id, rx.frame.payload_len);
            ESP_LOGI(TAG, "LinkIt device state=%d", (int)linkit_get_state());
        }

        TickType_t config_now = xTaskGetTickCount();
        if (!config_task_logged) {
            ESP_LOGI(TAG, "module config task active tick=%lu last=%lu state=%d",
                     (unsigned long)config_now, (unsigned long)last_config_tick,
                     (int)linkit_get_state());
            config_task_logged = true;
        }
        if (!last_config_tick || (config_now - last_config_tick) >= pdMS_TO_TICKS(POGO_MODULE_CONFIG_INTERVAL_MS)) {
            last_config_tick = config_now;
            linkit_frame_t config_frame = {
                .module_id = POGO_MODULE_ID,
                .msg_type = LINKIT_MSG_STREAM,
                .flags = LINKIT_FLAG_NONE,
                .cmd_id = POGO_CMD_MODULE_CONFIG,
                .seq = 0,
                .payload = (const uint8_t *)s_module_transport_config,
                .payload_len = (uint16_t)(sizeof(s_module_transport_config) - 1U),
            };
            const esp_err_t config_send_err = linkit_write_frame(&config_frame, 0);
            if (!s_transport_config_attempt_logged) {
                ESP_LOGI(TAG, "module config attempt linkit_state=%d mode=%s package=%s result=%s",
                         (int)linkit_get_state(), MODULE_TRANSPORT_MODE, MODULE_PACKAGE_TRANSPORT,
                         esp_err_to_name(config_send_err));
                s_transport_config_attempt_logged = true;
            }
            if (config_send_err == ESP_OK && !s_transport_config_logged) {
                ESP_LOGI(TAG, "module transport config sent mode=%s package=%s result=ESP_OK",
                         MODULE_TRANSPORT_MODE, MODULE_PACKAGE_TRANSPORT);
                s_transport_config_logged = true;
                config_send_failure_logged = false;
            } else if (config_send_err != ESP_OK && config_send_err != ESP_ERR_LINKIT_NOT_LINKED &&
                       !config_send_failure_logged) {
                ESP_LOGW(TAG, "module transport config send failed result=%s", esp_err_to_name(config_send_err));
                config_send_failure_logged = true;
            }
        }

        if (linkit_get_state() == LINKIT_STATE_WIRED) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_hb_tick) >= pdMS_TO_TICKS(POGO_HEARTBEAT_MS)) {
                last_hb_tick = now;
                linkit_frame_t hb = {
                    .module_id   = POGO_MODULE_ID,
                    .msg_type    = LINKIT_MSG_STREAM,
                    .flags       = LINKIT_FLAG_NONE,
                    .cmd_id      = POGO_CMD_KEEPALIVE,
                    .seq         = 0,
                    .payload     = NULL,
                    .payload_len = 0,
                };
                (void)linkit_write_frame(&hb, 0);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));

        TickType_t now_tick = xTaskGetTickCount();
        if ((now_tick - last_log_tick) < pdMS_TO_TICKS(2000)) {
            continue;
        }
        last_log_tick = now_tick;

        linkit_link_status_t st = {0};
        if (linkit_get_link_status(&st) != ESP_OK) {
            ESP_LOGW(TAG, "[LINK] status unavailable");
            continue;
        }

        if (st.wired_state == LINKIT_FLOW_S4_DUAL_MODE_RUNTIME) {
            if (!logged_wired) {
                ESP_LOGI(TAG, "==== HANDSHAKE OK -> WIRED ====");
                logged_wired = true;
            }
            ESP_LOGI(TAG, "[LINK] %s peer=%02X:%02X:%02X:%02X:%02X:%02X type=%s mtu=%u",
                     flow_state_label(st.wired_state),
                     st.peer_mac[0], st.peer_mac[1], st.peer_mac[2],
                     st.peer_mac[3], st.peer_mac[4], st.peer_mac[5],
                     st.module_type, st.active_mtu);
        } else if (st.wired_state != last_wired) {
            ESP_LOGW(TAG, "[LINK] %s -> %s  waiting for handshake",
                     flow_state_label(last_wired), flow_state_label(st.wired_state));
            logged_wired = false;
            last_config_tick = 0;
        }
        last_wired = st.wired_state;
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(init_nvs());
    ESP_ERROR_CHECK(init_device_det());
    const esp_err_t sensor_err = pi_sensor_device_start();
    if (sensor_err != ESP_OK) ESP_LOGW(TAG, "sensor startup failed: %s; continuing communications", esp_err_to_name(sensor_err));
    power_guard_start();

    if (lcd_st7789_init() == ESP_OK && lcd_st7789_start() == ESP_OK) {
        ui_env_start();
        ESP_LOGI(TAG, "LCD UI started");
    } else {
        ESP_LOGW(TAG, "LCD init failed; continuing without display");
    }

#if CONFIG_APP_PI_DATA_ENABLE
    esp_err_t netif_err = esp_netif_init();
    if (netif_err != ESP_OK && netif_err != ESP_ERR_INVALID_STATE)
        ESP_LOGW(TAG, "ESP-NETIF init failed: %s; Pi networking will retry while sensors/LinkIt remain active",
                 esp_err_to_name(netif_err));
    esp_err_t event_loop_err = esp_event_loop_create_default();
    if (event_loop_err != ESP_OK && event_loop_err != ESP_ERR_INVALID_STATE)
        ESP_LOGW(TAG, "default event loop init failed: %s", esp_err_to_name(event_loop_err));

    s_pi_data_config = (module_data_config_t){
        .module_id = POGO_MODULE_ID,
        .module_instance = CONFIG_APP_PI_DATA_INSTANCE,
        .token = CONFIG_APP_PI_DATA_TOKEN,
        .port = CONFIG_APP_PI_DATA_PORT,
    };
    module_data_service_set_package_reader(embedded_package_read, NULL);
    const esp_err_t service_err = module_data_service_start(&s_pi_data_config, pi_command_callback, NULL);
    if (service_err == ESP_OK) {
        s_module_data_service_started = true;
#if CONFIG_APP_PI_TRANSPORT_BLE
        const esp_err_t ble_err = module_data_service_start_ble(CONFIG_APP_PI_DATA_BLE_NAME);
        s_ble_data_service_started = ble_err == ESP_OK;
        if (ble_err != ESP_OK)
            ESP_LOGW(TAG, "BLE data service unavailable: %s; LinkIt/sensors remain active", esp_err_to_name(ble_err));
#endif
    } else {
        ESP_LOGW(TAG, "module data service start failed: %s; LinkIt/sensors remain active, retry scheduled",
                 esp_err_to_name(service_err));
    }
#endif

    linkit_device_config_t config = LINKIT_DEVICE_CONFIG_DEFAULT();
    config.device_ext_desc = &s_device_desc;
    ESP_ERROR_CHECK(linkit_device_install(&config));

    BaseType_t ok = xTaskCreate(linkit_task, "linkit_service",
                                LINKIT_TASK_STACK_BYTES, NULL,
                                LINKIT_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create LinkIt service task");
        return;
    }

#if CONFIG_APP_PI_DATA_ENABLE
    ok = xTaskCreate(pi_data_task, "pi_data_test", 4096, NULL, 4, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create Orange Pi data task");
        return;
    }
    ok = xTaskCreate(disconnect_button_task, "disconnect_button", 8192, NULL, 4, NULL);
    if (ok != pdPASS) ESP_LOGE(TAG, "failed to create module disconnect button task");
    if (!s_package_http_started || !s_module_data_service_started
#if CONFIG_APP_PI_TRANSPORT_BLE
        || !s_ble_data_service_started
#endif
        ) {
        ok = xTaskCreate(wifi_recovery_task, "data_service_recovery", 4096, NULL, 3, NULL);
        if (ok != pdPASS) ESP_LOGE(TAG, "failed to create data service recovery task; sensors/LinkIt remain active");
    }
#endif

    ESP_LOGI(TAG, "pogo device ready: TX=GPIO%d RX=GPIO%d DET=GPIO%d (module_id=0x%02X)",
             POGO_PIN_TX, POGO_PIN_RX, POGO_PIN_DET, POGO_MODULE_ID);
}
