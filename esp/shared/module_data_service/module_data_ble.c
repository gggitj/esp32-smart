#include "module_data_ble.h"

#include <string.h>
#include <stdlib.h>

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (-(int32_t)(crc & 1U)));
    }
    return ~crc;
}

static void put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

bool module_data_ble_encode(uint16_t message_id, const uint8_t *payload, size_t length,
                            bool (*emit)(const uint8_t *, size_t, void *), void *context)
{
    if ((!payload && length) || !emit || length > 65532U) return false;
    const size_t wire_length = length + 4U;
    uint8_t *wire = malloc(wire_length);
    if (!wire) return false;
    if (length) memcpy(wire, payload, length);
    const uint32_t sum = crc32(payload, length);
    wire[length] = (uint8_t)sum; wire[length + 1] = (uint8_t)(sum >> 8);
    wire[length + 2] = (uint8_t)(sum >> 16); wire[length + 3] = (uint8_t)(sum >> 24);
    const size_t count = (wire_length + MODULE_DATA_BLE_MTU_PAYLOAD - 1U) / MODULE_DATA_BLE_MTU_PAYLOAD;
    if (count == 0 || count > UINT16_MAX) { free(wire); return false; }
    for (size_t index = 0; index < count; ++index) {
        const size_t offset = index * MODULE_DATA_BLE_MTU_PAYLOAD;
        const size_t chunk = wire_length - offset > MODULE_DATA_BLE_MTU_PAYLOAD
                           ? MODULE_DATA_BLE_MTU_PAYLOAD : wire_length - offset;
        uint8_t frame[MODULE_DATA_BLE_FRAME_SIZE] = {0};
        put_le16(frame, message_id); put_le16(frame + 2, (uint16_t)index);
        put_le16(frame + 4, (uint16_t)count);
        if (chunk) memcpy(frame + 6, wire + offset, chunk);
        if (!emit(frame, 6U + chunk, context)) { free(wire); return false; }
    }
    free(wire);
    return true;
}

bool module_data_ble_decode(const uint8_t *fragment, size_t length,
                            module_data_ble_fragment_t *decoded)
{
    if (!fragment || !decoded || length < 6 || length > MODULE_DATA_BLE_FRAME_SIZE) return false;
    decoded->id = get_le16(fragment); decoded->index = get_le16(fragment + 2);
    decoded->count = get_le16(fragment + 4); decoded->length = length - 6;
    if (!decoded->count || decoded->index >= decoded->count || decoded->length > MODULE_DATA_BLE_MTU_PAYLOAD) return false;
    memcpy(decoded->data, fragment + 6, decoded->length);
    return true;
}

void module_data_ble_reassembler_reset(module_data_ble_reassembler_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

bool module_data_ble_reassembler_push(module_data_ble_reassembler_t *state,
                                      const uint8_t *fragment, size_t length,
                                      const uint8_t **payload, size_t *payload_length)
{
    module_data_ble_fragment_t decoded;
    if (!state || !payload || !payload_length || !module_data_ble_decode(fragment, length, &decoded)) return false;
    if (decoded.index == 0) {
        module_data_ble_reassembler_reset(state);
        state->message_id = decoded.id; state->count = decoded.count;
    }
    if (decoded.id != state->message_id || decoded.count != state->count || decoded.index != state->next_index ||
        state->length + decoded.length > sizeof(state->buffer)) { module_data_ble_reassembler_reset(state); return false; }
    memcpy(state->buffer + state->length, decoded.data, decoded.length);
    state->length += decoded.length; ++state->next_index;
    if (state->next_index != state->count) return false;
    if (state->length < 4 || crc32(state->buffer, state->length - 4) !=
        ((uint32_t)state->buffer[state->length - 4] | ((uint32_t)state->buffer[state->length - 3] << 8) |
         ((uint32_t)state->buffer[state->length - 2] << 16) | ((uint32_t)state->buffer[state->length - 1] << 24))) {
        module_data_ble_reassembler_reset(state); return false;
    }
    *payload = state->buffer; *payload_length = state->length - 4;
    state->length -= 4;
    state->next_index = 0; state->count = 0;
    return true;
}
