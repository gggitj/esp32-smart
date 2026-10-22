#include "esp_check.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "sdkconfig.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "linkit.h"
#include "linkit_pins.h"

#define LINKIT_TASK_STACK_BYTES 8192U
#define LINKIT_TASK_PRIORITY    8U

#define POGO_HOST_MODULE_ID     0x00u /* module_id used when host sends frames */
#define POGO_CMD_KEEPALIVE      0x7Eu
#define POGO_CMD_MODULE_CONFIG  0x03u
#define POGO_HEARTBEAT_MS       1000U

/* The S3 devkit CH340 is wired to UART0 (GPIO43/44). LinkIt stays on UART1. */
#define PBRIDGE_UART            UART_NUM_0
#define PBRIDGE_BAUD            115200
#define PBRIDGE_ANNOUNCE_MS      3000U
#define PBRIDGE_TX_GPIO         43
#define PBRIDGE_RX_GPIO         44
#define PBRIDGE_MODULE_ID       CONFIG_PBRIDGE_MODULE_ID
#define PBRIDGE_INSTANCE        CONFIG_PBRIDGE_INSTANCE
#define PBRIDGE_MAX_PAYLOAD     2048U

static const char *TAG = "POGO_HOST";
static uint32_t s_pbr_sequence = 1;
static uint8_t s_control_rx[14 + PBRIDGE_MAX_PAYLOAD];
static size_t s_control_rx_length;
static int64_t s_control_partial_since;
static uint32_t s_control_sequence;
static bool s_have_control_sequence;
static bool s_contact_attached;
static bool s_module_config_valid;
static char s_module_transport_mode[32];
static char s_module_package_transport[24];

static uint16_t pbr_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
    return crc;
}

static void pbr_put_le16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
}

static void pbr_put_le32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

static bool pbr_send_frame(uint8_t kind, const char *json)
{
    if (!json || !s_pbr_sequence) return false;
    const size_t length = strlen(json);
    if (length > PBRIDGE_MAX_PAYLOAD) return false;
    uint8_t frame[14 + PBRIDGE_MAX_PAYLOAD];
    memcpy(frame, "PBR1", 4); frame[4] = 1; frame[5] = kind;
    pbr_put_le32(frame + 6, s_pbr_sequence++);
    pbr_put_le16(frame + 10, (uint16_t)length);
    memcpy(frame + 12, json, length);
    pbr_put_le16(frame + 12 + length, pbr_crc16(frame + 4, 8 + length));
    size_t sent = 0;
    while (sent < 14 + length) {
        const int written = uart_write_bytes(PBRIDGE_UART, frame + sent, 14 + length - sent);
        if (written <= 0) return false;
        sent += (size_t)written;
    }
    return uart_wait_tx_done(PBRIDGE_UART, pdMS_TO_TICKS(100)) == ESP_OK;
}

static bool pbr_send_link_event(bool attached, uint8_t module_id)
{
    char json[512];
    int json_len;
    if (attached) {
        if (!s_module_config_valid) return false;
        json_len = snprintf(json, sizeof(json),
            "{\"event\":\"attach\",\"module_id\":%u,\"module_instance\":\"%s\","
            "\"module_config\":{\"schema\":\"module-transport-v1\",\"transport_mode\":\"%s\",\"package_transport\":\"%s\"}}",
            module_id, PBRIDGE_INSTANCE, s_module_transport_mode, s_module_package_transport);
    } else {
        json_len = snprintf(json, sizeof(json),
            "{\"event\":\"detach\",\"module_id\":%u,\"module_instance\":\"%s\"}",
            module_id, PBRIDGE_INSTANCE);
    }
    if (json_len <= 0 || (size_t)json_len >= sizeof(json)) return false;
    return pbr_send_frame(1, json);
}

static void pbr_handle_command(const uint8_t *payload, size_t length, uint32_t sequence)
{
    // Diagnostic commands are small. Bound parser recursion independently of
    // the framing limit so deeply nested JSON cannot exhaust the service stack.
    if (!length || length > 256 || memchr(payload, '\0', length)) return;
    unsigned depth = 0;
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < length; ++i) {
        const char ch = (char)payload[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
        } else if (ch == '"') quoted = true;
        else if (ch == '{' || ch == '[') { if (++depth > 8) return; }
        else if (ch == '}' || ch == ']') { if (!depth) return; --depth; }
    }
    char text[257];
    memcpy(text, payload, length); text[length] = '\0';
    cJSON *object = cJSON_ParseWithLengthOpts(text, length + 1, NULL, true);
    if (!cJSON_IsObject(object)) {
        cJSON_Delete(object);
        (void)pbr_send_frame(3, "invalid control JSON"); return;
    }
    const cJSON *command = cJSON_GetObjectItemCaseSensitive(object, "command");
    const bool ping = (cJSON_IsString(command) && !strcmp(command->valuestring, "ping")) ||
                      (cJSON_IsNumber(command) && command->valuedouble == 1);
    const bool status = cJSON_IsString(command) && !strcmp(command->valuestring, "status");
    if (ping || status) {
        char reply[192];
        snprintf(reply, sizeof(reply),
                 "{\"event\":\"%s\",\"request_sequence\":%lu,\"attached\":%s,\"module_id\":%u}",
                 ping ? "pong" : "bridge_status", (unsigned long)sequence,
                 s_contact_attached ? "true" : "false", PBRIDGE_MODULE_ID);
        (void)pbr_send_frame(1, reply);
    } else {
        // Do not silently accept business commands or firmware/network writes.
        (void)pbr_send_frame(3, "unsupported control command");
    }
    cJSON_Delete(object);
}

static void pbr_poll_control(void)
{
    const int64_t now = esp_timer_get_time();
    // Expire a stalled frame even when there are no newly received bytes.
    if (s_control_partial_since && now - s_control_partial_since >= 1000000) {
        s_control_rx_length = 0; s_control_partial_since = 0;
    }
    uint8_t input[256];
    const int count = uart_read_bytes(PBRIDGE_UART, input, sizeof(input), 0);
    if (count > 0) {
        if (s_control_rx_length + (size_t)count > sizeof(s_control_rx)) {
            s_control_rx_length = 0; s_control_partial_since = 0;
        }
        if (!s_control_rx_length) s_control_partial_since = now;
        memcpy(s_control_rx + s_control_rx_length, input, (size_t)count);
        s_control_rx_length += (size_t)count;
    }
    while (s_control_rx_length >= 4) {
        if (memcmp(s_control_rx, "PBR1", 4) != 0) {
            memmove(s_control_rx, s_control_rx + 1, --s_control_rx_length);
            continue;
        }
        if (s_control_rx_length < 14) break;
        const uint16_t length = (uint16_t)s_control_rx[10] | ((uint16_t)s_control_rx[11] << 8);
        if (length > PBRIDGE_MAX_PAYLOAD || s_control_rx[4] != 0 || s_control_rx[5] != 2) {
            memmove(s_control_rx, s_control_rx + 1, --s_control_rx_length);
            continue;
        }
        const size_t total = 14 + length;
        if (s_control_rx_length < total) break;
        const uint16_t checksum = (uint16_t)s_control_rx[12 + length] | ((uint16_t)s_control_rx[13 + length] << 8);
        if (pbr_crc16(s_control_rx + 4, 8 + length) != checksum) {
            memmove(s_control_rx, s_control_rx + 1, --s_control_rx_length);
            continue;
        }
        const uint32_t sequence = (uint32_t)s_control_rx[6] | ((uint32_t)s_control_rx[7] << 8) |
            ((uint32_t)s_control_rx[8] << 16) | ((uint32_t)s_control_rx[9] << 24);
        const uint32_t distance = sequence - s_control_sequence;
        if (sequence && (!s_have_control_sequence || (distance && distance < 0x80000000u))) {
            s_control_sequence = sequence; s_have_control_sequence = true;
            pbr_handle_command(s_control_rx + 12, length, sequence);
        }
        memmove(s_control_rx, s_control_rx + total, s_control_rx_length - total);
        s_control_rx_length -= total;
        s_control_partial_since = s_control_rx_length ? now : 0;
    }
    if (!s_control_rx_length) s_control_partial_since = 0;
}

static void pbr_init(void)
{
    const uart_config_t config = {
        .baud_rate = PBRIDGE_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(PBRIDGE_UART, 2048, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(PBRIDGE_UART, &config));
    ESP_ERROR_CHECK(uart_set_pin(PBRIDGE_UART, PBRIDGE_TX_GPIO, PBRIDGE_RX_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

static esp_err_t init_nvs(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_RETURN_ON_ERROR(nvs_flash_erase(), TAG, "erase NVS failed");
        err = nvs_flash_init();
    }
    return err;
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
    linkit_rx_frame_t rx;
    bool pbr_attached = false;
    linkit_state_t last_linkit_state = LINKIT_STATE_DISCONNECTED;
    TickType_t last_pbr_announce_tick = 0;
    TickType_t removal_since = 0;
    uint8_t pbr_module_id = PBRIDGE_MODULE_ID;

    ESP_LOGI(TAG, "LinkIt host pump started, DET pulled-up: waiting for module attach...");

    while (true) {
        pbr_poll_control();
        ESP_ERROR_CHECK_WITHOUT_ABORT(linkit_poll(0));

        while (linkit_read_frame(&rx, 0) == ESP_OK) {
            ESP_LOGI(TAG, "rx frame module=0x%02X msg=%u flags=0x%02X cmd=0x%02X len=%u",
                     rx.frame.module_id, rx.frame.msg_type, rx.frame.flags,
                     rx.frame.cmd_id, rx.frame.payload_len);
            if (rx.frame.cmd_id == POGO_CMD_MODULE_CONFIG &&
                rx.frame.payload_len > 0 && rx.frame.payload_len < 256 &&
                !memchr(rx.payload, '\0', rx.frame.payload_len)) {
                char config_text[256];
                memcpy(config_text, rx.payload, rx.frame.payload_len);
                config_text[rx.frame.payload_len] = '\0';
                cJSON *config = cJSON_ParseWithLength(config_text, rx.frame.payload_len);
                const cJSON *schema = cJSON_GetObjectItemCaseSensitive(config, "schema");
                const cJSON *mode = cJSON_GetObjectItemCaseSensitive(config, "transport_mode");
                const cJSON *package = cJSON_GetObjectItemCaseSensitive(config, "package_transport");
                const bool valid = cJSON_IsObject(config) && cJSON_IsString(schema) &&
                    strcmp(schema->valuestring, "module-transport-v1") == 0 &&
                    cJSON_IsString(mode) &&
                    (!strcmp(mode->valuestring, "fixed_ble") || !strcmp(mode->valuestring, "fixed_hotspot_lan")) &&
                    cJSON_IsString(package) &&
                    (!strcmp(package->valuestring, "data") || !strcmp(package->valuestring, "hotspot_lan"));
                const bool combo_valid = valid &&
                    (!strcmp(mode->valuestring, "fixed_ble") || !strcmp(package->valuestring, "data"));
                if (combo_valid) {
                    strlcpy(s_module_transport_mode, mode->valuestring, sizeof(s_module_transport_mode));
                    strlcpy(s_module_package_transport, package->valuestring, sizeof(s_module_package_transport));
                    s_module_config_valid = true;
                    ESP_LOGI(TAG, "module transport config accepted mode=%s package=%s",
                             s_module_transport_mode, s_module_package_transport);
                } else {
                    s_module_config_valid = false;
                    ESP_LOGW(TAG, "reject invalid module transport configuration frame");
                    (void)pbr_send_frame(3, "invalid module transport configuration");
                }
                cJSON_Delete(config);
            }
        }

        const linkit_state_t linkit_state = linkit_get_state();
        if (linkit_state != last_linkit_state) {
            if (linkit_state != LINKIT_STATE_WIRED && s_module_config_valid) {
                ESP_LOGI(TAG, "LinkIt left wired state; discard cached module transport config");
                s_module_config_valid = false;
            }
            last_linkit_state = linkit_state;
        }

        if (linkit_get_state() == LINKIT_STATE_WIRED) {
            TickType_t now = xTaskGetTickCount();
            if ((now - last_hb_tick) >= pdMS_TO_TICKS(POGO_HEARTBEAT_MS)) {
                last_hb_tick = now;
                linkit_frame_t hb = {
                    .module_id   = POGO_HOST_MODULE_ID,
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

        linkit_link_status_t pbr_status = {0};
        const bool status_valid = linkit_get_link_status(&pbr_status) == ESP_OK;
        const bool contact_present = gpio_get_level(POGO_PIN_DET) == 0;
        // Once a module has been identified, a LinkIt recovery/temporary status
        // failure is not a physical eject. Report detach from debounced DET.
        const bool attached = contact_present && (pbr_attached ||
            (status_valid && pbr_status.wired_state == LINKIT_FLOW_S4_DUAL_MODE_RUNTIME));
        const TickType_t pbr_now = xTaskGetTickCount();
        if (contact_present) removal_since = 0;
        else if (!removal_since) removal_since = pbr_now;
        if (!contact_present) s_module_config_valid = false;
        if (attached && (!pbr_attached ||
            (pbr_now - last_pbr_announce_tick) >= pdMS_TO_TICKS(PBRIDGE_ANNOUNCE_MS))) {
            /* LinkIt status.module_id is the peer's generic LinkIt type (1),
             * not the Pi product/profile ID. Map this tested module explicitly. */
            pbr_module_id = PBRIDGE_MODULE_ID;
            if (pbr_send_link_event(true, pbr_module_id)) {
                pbr_attached = true; s_contact_attached = true;
                last_pbr_announce_tick = pbr_now;
            }
        } else if (!contact_present && pbr_attached &&
                   (pbr_now - removal_since) >= pdMS_TO_TICKS(100)) {
            if (pbr_send_link_event(false, pbr_module_id)) {
                pbr_attached = false; s_contact_attached = false;
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
            ESP_LOGI(TAG, "[LINK] %s peer=%02X:%02X:%02X:%02X:%02X:%02X type=%s id=0x%02X mtu=%u",
                     flow_state_label(st.wired_state),
                     st.peer_mac[0], st.peer_mac[1], st.peer_mac[2],
                     st.peer_mac[3], st.peer_mac[4], st.peer_mac[5],
                     st.module_type, st.module_id, st.active_mtu);
        } else if (st.wired_state != last_wired) {
            ESP_LOGW(TAG, "[LINK] %s -> %s  waiting for handshake",
                     flow_state_label(last_wired), flow_state_label(st.wired_state));
            logged_wired = false;
        }
        last_wired = st.wired_state;
    }
}

void app_main(void)
{
    const char *instance = PBRIDGE_INSTANCE;
    if (!instance[0] || strlen(instance) > 64) abort();
    for (const char *p = instance; *p; ++p)
        if (!((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) abort();
    pbr_init();
    ESP_ERROR_CHECK(init_nvs());

    linkit_host_config_t config = LINKIT_HOST_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(linkit_host_install(&config));

    BaseType_t ok = xTaskCreate(linkit_task, "linkit_service",
                                LINKIT_TASK_STACK_BYTES, NULL,
                                LINKIT_TASK_PRIORITY, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "failed to create LinkIt service task");
        return;
    }

    ESP_LOGI(TAG, "pogo host ready: TX=GPIO%d RX=GPIO%d DET=GPIO%d",
             POGO_PIN_TX, POGO_PIN_RX, POGO_PIN_DET);
}
