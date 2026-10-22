#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MODULE_DATA_BLE_MTU_PAYLOAD 14U
#define MODULE_DATA_BLE_FRAME_SIZE 20U

typedef struct {
    uint16_t id;
    uint16_t index;
    uint16_t count;
    uint8_t data[MODULE_DATA_BLE_MTU_PAYLOAD];
    size_t length;
} module_data_ble_fragment_t;

// Encodes one application message into Pi-terminal-compatible GATT fragments.
// The callback is called once per fragment and must copy/send it synchronously.
bool module_data_ble_encode(uint16_t message_id, const uint8_t *payload, size_t length,
                            bool (*emit)(const uint8_t *fragment, size_t length, void *context),
                            void *context);

// Decodes one 20-byte GATT notification. The caller enforces ordering and
// checksum after collecting all fragments; this function only validates bounds.
bool module_data_ble_decode(const uint8_t *fragment, size_t length,
                            module_data_ble_fragment_t *decoded);

typedef struct {
    uint16_t message_id;
    uint16_t next_index;
    uint16_t count;
    size_t length;
    uint8_t buffer[65536];
} module_data_ble_reassembler_t;

void module_data_ble_reassembler_reset(module_data_ble_reassembler_t *state);
// Returns true only when a complete CRC-checked application message is ready.
bool module_data_ble_reassembler_push(module_data_ble_reassembler_t *state,
                                      const uint8_t *fragment, size_t length,
                                      const uint8_t **payload, size_t *payload_length);
