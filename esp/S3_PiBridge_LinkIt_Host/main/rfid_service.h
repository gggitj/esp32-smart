#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef bool (*rfid_send_fn)(uint8_t kind, const char *json);

typedef struct {
    bool enabled;
    bool online;
    uint8_t version;
    uint32_t reports;
    uint32_t read_errors;
    uint32_t tx_failures;
    esp_err_t last_error;
} rfid_status_t;

esp_err_t rfid_service_start(rfid_send_fn send);
rfid_status_t rfid_service_status(void);
