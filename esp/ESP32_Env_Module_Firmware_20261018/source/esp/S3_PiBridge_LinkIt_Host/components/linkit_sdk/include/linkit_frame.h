#ifndef LINKIT_FRAME_H
#define LINKIT_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINKIT_FRAME_HEAD                0xAAU
#define LINKIT_FRAME_VERSION             0x01U
#define LINKIT_FRAME_HEADER_SIZE         9U
#define LINKIT_FRAME_CRC_SIZE            2U
#define LINKIT_FRAME_PROTOCOL_MAX_PAYLOAD 1024U

#ifndef LINKIT_FRAME_MAX_PAYLOAD
#define LINKIT_FRAME_MAX_PAYLOAD LINKIT_FRAME_PROTOCOL_MAX_PAYLOAD
#endif

#if LINKIT_FRAME_MAX_PAYLOAD > LINKIT_FRAME_PROTOCOL_MAX_PAYLOAD
#error "LINKIT_FRAME_MAX_PAYLOAD must not exceed 1024 bytes in protocol v2.1"
#endif

#define LINKIT_FRAME_MAX_WIRE_SIZE (LINKIT_FRAME_HEADER_SIZE + LINKIT_FRAME_MAX_PAYLOAD + LINKIT_FRAME_CRC_SIZE)
#define LINKIT_FRAME_RESERVED_FLAG_MASK 0x0CU

typedef enum {
    LINKIT_MSG_REQ    = 0x0,
    LINKIT_MSG_RSP    = 0x1,
    LINKIT_MSG_EVT    = 0x2,
    LINKIT_MSG_STREAM = 0x3,
    LINKIT_MSG_ACK    = 0x4,
    LINKIT_MSG_NAK    = 0x5,
    LINKIT_MSG_CTRL   = 0x6,
    /* Legacy alias kept to avoid breaking v2.0 draft callers. */
    LINKIT_MSG_DATA   = LINKIT_MSG_STREAM,
} linkit_msg_type_t;

typedef enum {
    LINKIT_FLAG_NONE     = 0x0,
    LINKIT_FLAG_FRAG     = 0x1,
    LINKIT_FLAG_NEED_ACK = 0x2,
    /* Legacy alias kept to avoid breaking early draft callers. */
    LINKIT_FLAG_FRAGMENT = LINKIT_FLAG_FRAG,
} linkit_frame_flags_t;

typedef enum {
    LINKIT_FRAME_OK = 0,
    LINKIT_FRAME_ERR_ARG,
    LINKIT_FRAME_ERR_PAYLOAD_TOO_LARGE,
    LINKIT_FRAME_ERR_BUFFER_TOO_SMALL,
    LINKIT_FRAME_ERR_BAD_CRC,
    LINKIT_FRAME_ERR_BAD_VERSION,
    LINKIT_FRAME_ERR_BAD_HEAD,
    LINKIT_FRAME_ERR_RESERVED_FLAGS,
    LINKIT_FRAME_ERR_UNKNOWN_MSG_TYPE,
    LINKIT_FRAME_ERR_INVALID_FLAG_COMBO,
} linkit_frame_status_t;

typedef struct {
    uint8_t module_id;
    uint8_t msg_type;
    uint8_t flags;
    uint8_t cmd_id;
    uint16_t seq;
    const uint8_t *payload;
    uint16_t payload_len;
} linkit_frame_t;

typedef struct {
    linkit_frame_t frame;
    uint8_t payload[LINKIT_FRAME_MAX_PAYLOAD];
} linkit_rx_frame_t;

#define LINKIT_GET_MSG_TYPE(type_flags) (((type_flags) >> 4) & 0x0FU)
#define LINKIT_GET_FLAGS(type_flags) ((type_flags) & 0x0FU)
#define LINKIT_MAKE_TYPE_FLAGS(type, flags) (uint8_t)((((type) & 0x0FU) << 4) | ((flags) & 0x0FU))

bool linkit_frame_msg_type_valid(uint8_t msg_type);
bool linkit_frame_flags_valid(uint8_t flags);
bool linkit_frame_combo_valid(uint8_t msg_type, uint8_t flags);
size_t linkit_frame_encoded_size(uint16_t payload_len);
size_t linkit_frame_encode(const linkit_frame_t *frame, uint8_t *out, size_t cap);
linkit_frame_status_t linkit_frame_decode(const uint8_t *wire,
                                          size_t wire_len,
                                          linkit_frame_t *out_frame,
                                          const uint8_t **out_payload);

#ifdef __cplusplus
}
#endif

#endif
