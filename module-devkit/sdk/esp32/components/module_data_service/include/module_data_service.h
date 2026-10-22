#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t module_id;
    const char *module_instance;
    const char *token;
    uint16_t port;
} module_data_config_t;

typedef bool (*module_data_command_cb_t)(const char *event, const char *json, void *context);
typedef bool (*module_data_wire_send_cb_t)(const uint8_t *payload, size_t length, void *context);
typedef size_t (*module_data_package_read_cb_t)(const char *file, uint32_t offset,
                                                uint8_t *buffer, size_t capacity,
                                                uint32_t *total, void *context);

// Convenience reader for a mounted filesystem directory containing the two
// package files. The directory must already be mounted by the board firmware.
size_t module_data_package_read_from_directory(const char *file, uint32_t offset,
                                               uint8_t *buffer, size_t capacity,
                                               uint32_t *total, void *context);

// The caller owns Wi-Fi initialization. Call this only after the station has
// acquired an address. The service accepts one Pi terminal connection at a time.
esp_err_t module_data_service_start(const module_data_config_t *config,
                                    module_data_command_cb_t command_callback,
                                    void *context);
esp_err_t module_data_service_start_ble(const char *device_name);
// Optional read-only HTTP endpoint for signed package files. Intended for
// hotspot package delivery while the authenticated business session remains
// on BLE; exposes only package.json and package.tar.zst from the reader.
esp_err_t module_data_service_start_package_http(uint16_t port, const char *allowed_peer_ipv4);
void module_data_service_stop_package_http(void);
void module_data_service_stop(void);
void module_data_service_set_package_reader(module_data_package_read_cb_t reader, void *context);
void module_data_service_set_ble_sender(module_data_wire_send_cb_t sender, void *context);
void module_data_service_receive(const uint8_t *payload, size_t length);
void module_data_service_receive_ble(const uint8_t *payload, size_t length);

// Publishes a raw module payload as the Pi terminal's version-1 data event.
// Payloads are Base64 encoded by the service and are bounded to 64 KiB.
esp_err_t module_data_service_publish(const uint8_t *payload, size_t length);
// Ask the host terminal to close this module session. The host responds with
// disconnect_ack; an accepted request is completed by session_close.
esp_err_t module_data_service_request_disconnect(const char *reason, bool auto_reconnect);

typedef struct {
    const char *ssid;
    const char *password;
    uint8_t max_retry;
    // Optional static IPv4 settings; when all are empty, DHCP is used.
    const char *static_ip;
    const char *netmask;
    const char *gateway;
} module_data_wifi_config_t;

// Initializes ESP-NETIF/Wi-Fi STA and waits for an address before returning.
// The caller remains responsible for avoiding conflicts with LinkIt ownership.
esp_err_t module_data_wifi_start(const module_data_wifi_config_t *config);
void module_data_wifi_stop(void);

/* Typical board integration after Wi-Fi STA/GATT setup:
 *
 *   module_data_config_t cfg = { .module_id = 0x14,
 *       .module_instance = "env-01", .token = CONFIG_PI_TOKEN, .port = 4580 };
 *   module_data_service_start(&cfg, on_command, app_context);
 *   module_data_service_set_package_reader(read_package, app_context);
 *
 * The service deliberately does not initialize Wi-Fi or mount storage because
 * those resources are board-specific and may already be owned by LinkIt.
 */
