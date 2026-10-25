/**
 * @file linkit_parser.c
 * @brief LinkIt v2.1 通用帧流式解析器
 *
 * @details
 * 解析器按字节消费输入流，自动寻找 0xAA 帧头，收齐 header、payload
 * 和 CRC 后通过回调上报完整帧。它适合 UART 这类字节流通道，也可以被
 * SPI/I2C 分片输入复用。
 */

#include "linkit_parser.h"

#include <string.h>

static uint16_t get_u16_le(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

/* uint32_t 差值写法可自然处理毫秒计数回绕。 */
static bool linkit_elapsed_exceeds(uint32_t now_ms, uint32_t start_ms, uint32_t timeout_ms)
{
    return (uint32_t)(now_ms - start_ms) > timeout_ms;
}

/* 回到空闲态后，下一字节必须重新寻找 0xAA 帧头。 */
static void parser_set_idle(linkit_parser_t *parser)
{
    parser->state = LINKIT_PARSER_WAIT_HEAD;
    parser->raw_len = 0U;
    parser->header_pos = 0U;
    parser->payload_pos = 0U;
    parser->payload_len = 0U;
    parser->module_id = 0U;
    parser->type_flags = 0U;
    parser->cmd_id = 0U;
    parser->seq = 0U;
    parser->last_byte_ms_valid = false;
    parser->resync_pending = false;
}

/**
 * @brief 清空解析器内部缓存并回到等待帧头状态。
 */
void linkit_parser_reset(linkit_parser_t *parser)
{
    if (parser == 0) {
        return;
    }

    memset(parser->header, 0, sizeof(parser->header));
    memset(parser->payload, 0, sizeof(parser->payload));
    memset(parser->crc_bytes, 0, sizeof(parser->crc_bytes));
    memset(parser->raw, 0, sizeof(parser->raw));
    parser_set_idle(parser);
}

/**
 * @brief 初始化流式解析器。
 * @param cb 收到完整合法帧后的回调，可为 NULL。
 */
void linkit_parser_init(linkit_parser_t *parser, linkit_on_frame_t cb, void *user)
{
    if (parser == 0) {
        return;
    }

    memset(parser, 0, sizeof(*parser));
    parser->on_frame = cb;
    parser->user = user;
    parser_set_idle(parser);
}

/* 保存原始字节用于最终 CRC 校验，长度受 LINKIT_FRAME_MAX_WIRE_SIZE 限制。 */
static void parser_append_raw(linkit_parser_t *parser, uint8_t byte)
{
    if (parser->raw_len < sizeof(parser->raw)) {
        parser->raw[parser->raw_len++] = byte;
    }
}

/* header 收齐后先做轻量校验，明显非法的帧立即丢弃并尝试从尾部恢复。 */
static bool parser_header_valid(const linkit_parser_t *parser)
{
    const uint8_t msg_type = LINKIT_GET_MSG_TYPE(parser->type_flags);
    const uint8_t flags = LINKIT_GET_FLAGS(parser->type_flags);

    if (parser->header[0] != LINKIT_FRAME_VERSION) {
        return false;
    }
    if (parser->payload_len > LINKIT_FRAME_MAX_PAYLOAD) {
        return false;
    }
    if (!linkit_frame_combo_valid(msg_type, flags)) {
        return false;
    }

    return true;
}

/*
 * 收齐整帧后做完整解码与 CRC 校验。
 *
 * 返回值只表示"这一帧是否合法"，与是否注册了回调无关：未注册回调时帧仍然
 * 是合法的，应当正常消费掉，而不是被当成坏帧触发重扫。
 */
static bool parser_emit_if_valid(linkit_parser_t *parser)
{
    linkit_frame_status_t status;
    linkit_frame_t frame;
    const uint8_t *payload_view = 0;

    status = linkit_frame_decode(parser->raw, parser->raw_len, &frame, &payload_view);
    if (status != LINKIT_FRAME_OK) {
        return false;
    }

    if (parser->on_frame != 0) {
        parser->on_frame(&frame, parser->user);
    }
    return true;
}

/*
 * 当前帧非法：只置位标志，真正的重扫由 parser_run_resync() 迭代完成。
 *
 * 这里不能直接回喂字节流，否则会与 parser_process_byte() 形成相互递归。
 */
static void parser_request_resync(linkit_parser_t *parser)
{
    parser->resync_pending = true;
}

static void parser_process_byte(linkit_parser_t *parser, uint8_t byte);

/*
 * 坏帧恢复：丢弃当前帧的首字节，在剩余原始字节中继续寻找下一个 0xAA。
 *
 * 实现为单层循环，不递归、不在栈上开缓冲区：
 *   - replay[] 保存本轮待重扫的字节，长度固定，循环期间不再改写；
 *   - frame_start 记录当前这一帧的 0xAA 在 replay[] 中的下标；
 *   - 帧再次非法时，把扫描位置退回 frame_start + 1 继续。
 *
 * 终止性：每次重扫都要求新的 frame_start 严格大于上一次的 frame_start，
 * 而 frame_start 上界为 len，因此重扫次数不超过 len 次，循环必然结束。
 */
static void parser_run_resync(linkit_parser_t *parser)
{
    size_t len = 0U;
    size_t pos = 0U;
    size_t frame_start = 0U;

    if (parser->raw_len > 1U) {
        len = parser->raw_len - 1U;
        memcpy(parser->replay, &parser->raw[1], len);
    }

    linkit_parser_reset(parser);

    while (pos < len) {
        if (parser->state == LINKIT_PARSER_WAIT_HEAD) {
            frame_start = pos;
        }

        parser_process_byte(parser, parser->replay[pos]);

        if (parser->resync_pending) {
            linkit_parser_reset(parser);   /* 内含 resync_pending = false */
            pos = frame_start + 1U;
        } else {
            pos++;
        }
    }
}

/**
 * @brief 设置帧内字节间超时。
 * @param gap_ms 0 表示关闭该检查。
 */
void linkit_parser_set_gap_timeout(linkit_parser_t *parser, uint16_t gap_ms)
{
    if (parser == 0) {
        return;
    }

    parser->gap_timeout_ms = gap_ms;
}

/**
 * @brief 在没有新字节时推进超时检查。
 */
void linkit_parser_tick(linkit_parser_t *parser, uint32_t now_ms)
{
    if (parser == 0 || parser->gap_timeout_ms == 0U || !parser->last_byte_ms_valid) {
        return;
    }

    if (parser->state != LINKIT_PARSER_WAIT_HEAD &&
        linkit_elapsed_exceeds(now_ms, parser->last_byte_ms, parser->gap_timeout_ms)) {
        linkit_parser_reset(parser);
    }
}

/* 单字节状态机：WAIT_HEAD -> HEADER -> PAYLOAD -> CRC0 -> CRC1。 */
static void parser_process_byte(linkit_parser_t *parser, uint8_t byte)
{
    switch (parser->state) {
    case LINKIT_PARSER_WAIT_HEAD:
        if (byte == LINKIT_FRAME_HEAD) {
            parser->state = LINKIT_PARSER_HEADER;
            parser->header_pos = 0U;
            parser->raw[0] = byte;
            parser->raw_len = 1U;
        }
        break;

    case LINKIT_PARSER_HEADER:
        parser_append_raw(parser, byte);
        parser->header[parser->header_pos++] = byte;
        if (parser->header_pos == sizeof(parser->header)) {
            parser->module_id = parser->header[1];
            parser->type_flags = parser->header[2];
            parser->cmd_id = parser->header[3];
            parser->seq = get_u16_le(&parser->header[4]);
            parser->payload_len = get_u16_le(&parser->header[6]);

            if (!parser_header_valid(parser)) {
                parser_request_resync(parser);
            } else if (parser->payload_len == 0U) {
                parser->state = LINKIT_PARSER_CRC0;
            } else {
                parser->state = LINKIT_PARSER_PAYLOAD;
                parser->payload_pos = 0U;
            }
        }
        break;

    case LINKIT_PARSER_PAYLOAD:
        parser_append_raw(parser, byte);
        parser->payload[parser->payload_pos++] = byte;
        if (parser->payload_pos == parser->payload_len) {
            parser->state = LINKIT_PARSER_CRC0;
        }
        break;

    case LINKIT_PARSER_CRC0:
        parser_append_raw(parser, byte);
        parser->crc_bytes[0] = byte;
        parser->state = LINKIT_PARSER_CRC1;
        break;

    case LINKIT_PARSER_CRC1:
        parser_append_raw(parser, byte);
        parser->crc_bytes[1] = byte;
        if (parser_emit_if_valid(parser)) {
            linkit_parser_reset(parser);
        } else {
            parser_request_resync(parser);
        }
        break;

    default:
        linkit_parser_reset(parser);
        break;
    }
}

/**
 * @brief 带时间戳输入一个字节，用于执行帧间超时恢复。
 */
void linkit_parser_feed_byte(linkit_parser_t *parser, uint8_t byte, uint32_t now_ms)
{
    if (parser == 0) {
        return;
    }

    if (parser->gap_timeout_ms != 0U &&
        parser->last_byte_ms_valid &&
        parser->state != LINKIT_PARSER_WAIT_HEAD &&
        linkit_elapsed_exceeds(now_ms, parser->last_byte_ms, parser->gap_timeout_ms)) {
        linkit_parser_reset(parser);
    }

    parser_process_byte(parser, byte);
    if (parser->resync_pending) {
        parser_run_resync(parser);
    }
    parser->last_byte_ms = now_ms;
    parser->last_byte_ms_valid = true;
}

/**
 * @brief 批量输入字节，不执行帧间超时检查。
 */
void linkit_parser_feed(linkit_parser_t *parser, const uint8_t *data, size_t len)
{
    if (parser == 0 || data == 0) {
        return;
    }

    for (size_t i = 0; i < len; ++i) {
        parser_process_byte(parser, data[i]);
        if (parser->resync_pending) {
            parser_run_resync(parser);
        }
    }
}
