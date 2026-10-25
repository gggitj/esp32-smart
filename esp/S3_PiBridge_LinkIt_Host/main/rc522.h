#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"

typedef struct {
    uint8_t bytes[10];
    uint8_t length;
} rc522_uid_t;

typedef struct {
    spi_device_handle_t spi;
    int reset_gpio;
    uint8_t version;
} rc522_t;

esp_err_t rc522_open(rc522_t *reader, int ss, int sck, int mosi, int miso, int rst);
esp_err_t rc522_reset(rc522_t *reader);
/* present is true for an RF response even if UID selection fails/collides. */
esp_err_t rc522_poll(rc522_t *reader, rc522_uid_t *uid, bool *present);
bool rc522_uid_hex(const rc522_uid_t *uid, char *output, size_t capacity);
