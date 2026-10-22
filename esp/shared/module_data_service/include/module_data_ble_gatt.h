#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef void (*module_data_ble_rx_cb_t)(const uint8_t *payload, size_t length, void *context);

// UUIDs are fixed by the module platform BLE data protocol.
esp_err_t module_data_ble_gatt_start(const char *device_name,
                                     module_data_ble_rx_cb_t receive,
                                     void *context);
void module_data_ble_gatt_stop(void);
bool module_data_ble_gatt_send(const uint8_t *payload, size_t length);
bool module_data_ble_gatt_send_callback(const uint8_t *payload, size_t length, void *context);
