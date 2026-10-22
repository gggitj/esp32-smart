#ifndef LINKIT_PARSER_H
#define LINKIT_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "linkit_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*linkit_on_frame_t)(const linkit_frame_t *frame, void *user);

typedef enum {
    LINKIT_PARSER_WAIT_HEAD = 0,
    LINKIT_PARSER_HEADER,
    LINKIT_PARSER_PAYLOAD,
    LINKIT_PARSER_CRC0,
    LINKIT_PARSER_CRC1,
} linkit_parser_state_t;

typedef struct {
    linkit_parser_state_t state;
    uint8_t header[LINKIT_FRAME_HEADER_SIZE - 1U];
    uint8_t payload[LINKIT_FRAME_MAX_PAYLOAD];
    uint8_t crc_bytes[LINKIT_FRAME_CRC_SIZE];
    uint8_t raw[LINKIT_FRAME_MAX_WIRE_SIZE];
    size_t raw_len;
    size_t header_pos;
    size_t payload_pos;
    uint16_t payload_len;
    uint8_t module_id;
    uint8_t type_flags;
    uint8_t cmd_id;
    uint16_t seq;
    linkit_on_frame_t on_frame;
    void *user;
    /*
     * Patent claim 8: drop the partial frame and search for the next 0xAA
     * when the inter-byte gap exceeds the configured threshold.
     * gap_timeout_ms == 0 disables the check (legacy mode).
     */
    uint16_t gap_timeout_ms;
    uint32_t last_byte_ms;
    bool     last_byte_ms_valid;
    /*
     * Bad-frame resynchronisation state.
     *
     * resync_pending is raised by the byte state machine when the current
     * frame turns out to be invalid; the feed loop then replays the buffered
     * bytes from replay[] looking for the next 0xAA.
     *
     * The replay buffer and the pending flag exist so that recovery is a
     * bounded iteration instead of mutual recursion between the state machine
     * and the feed entry points. The recursive form allocated one
     * LINKIT_FRAME_MAX_WIRE_SIZE array per nesting level on the task stack and
     * overflowed a 4 KB stack after only a few nested bad frames.
     */
    uint8_t  replay[LINKIT_FRAME_MAX_WIRE_SIZE];
    bool     resync_pending;
} linkit_parser_t;

void linkit_parser_init(linkit_parser_t *parser, linkit_on_frame_t cb, void *user);
void linkit_parser_reset(linkit_parser_t *parser);

/*
 * Configure the inter-byte gap timeout in milliseconds. Pass 0 to disable.
 * Default after init is 0 to keep the legacy behaviour for callers that
 * have no millisecond clock available.
 */
void linkit_parser_set_gap_timeout(linkit_parser_t *parser, uint16_t gap_ms);

/*
 * Time-aware feed used to enforce the patent half-frame timeout. now_ms is
 * a monotonic millisecond counter; wraparound of uint32_t is supported.
 */
void linkit_parser_feed_byte(linkit_parser_t *parser, uint8_t byte, uint32_t now_ms);

/*
 * Drive the timeout without new bytes. Call from an idle loop or timer when
 * the parser may otherwise sit mid-frame indefinitely.
 */
void linkit_parser_tick(linkit_parser_t *parser, uint32_t now_ms);

/*
 * Legacy batch feed. Does not enforce the gap timeout because no time
 * source is supplied. Prefer linkit_parser_feed_byte for new code.
 */
void linkit_parser_feed(linkit_parser_t *parser, const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif
