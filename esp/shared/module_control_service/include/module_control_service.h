#pragma once

#include "esp_err.h"
#include <stdint.h>

typedef struct {
    uint8_t module_id;
    const char *transport_mode;   // fixed_ble or fixed_hotspot_lan
    const char *package_transport; // data or hotspot_lan
    const char *module_type;
} module_control_service_config_t;

// Installs the LinkIt device role, configures module DET, and starts the
// handshake/config/heartbeat task. The task owns no application payloads.
esp_err_t module_control_service_start(const module_control_service_config_t *config);
void module_control_service_stop(void);
