#pragma once

// Public ESP32 convenience layer. The platform-provided module_data_service
// remains the protocol implementation; these wrappers do not fork that ABI.
#include "module_data_service.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static inline esp_err_t module_sdk_publish_json(const char *json) {
    if (!json) return ESP_ERR_INVALID_ARG;
    return module_data_service_publish((const uint8_t *)json, strlen(json));
}

static inline esp_err_t module_sdk_disconnect(const char *reason, bool reconnect) {
    return module_data_service_request_disconnect(reason, reconnect);
}
