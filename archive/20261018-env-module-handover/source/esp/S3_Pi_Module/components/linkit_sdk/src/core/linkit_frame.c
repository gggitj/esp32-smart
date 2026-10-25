/**
 * @file linkit_frame.c
 * @brief LinkIt v2.1 通用数据帧编解码
 *
 * @details
 * 本文件只处理“线上帧格式”：0xAA 帧头、版本、module_id、消息类型、
 * cmd、seq、payload_len、payload 和 CRC。它不关心 UART/SPI/ESP-NOW
 * 具体传输方式，也不推进握手状态机。
 */

#include "linkit_frame.h"

#include <string.h>

#include "linkit_crc16.h"

static void put_u16_le(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xFFU);
    out[1] = (uint8_t)((value >> 8) & 0xFFU);
}

/* 协议统一使用小端字段，避免不同编译器结构体布局影响线上格式。 */
static uint16_t get_u16_le(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

/**
 * @brief 检查消息类型是否属于协议定义范围。
 */
bool linkit_frame_msg_type_valid(uint8_t msg_type)
{
    return msg_type <= LINKIT_MSG_CTRL;
}

/**
 * @brief 检查 flags 是否只使用了当前版本允许的位。
 */
bool linkit_frame_flags_valid(uint8_t flags)
{
    return (flags & LINKIT_FRAME_RESERVED_FLAG_MASK) == 0U;
}

/**
 * @brief 检查消息类型和 flags 的组合是否合法。
 */
bool linkit_frame_combo_valid(uint8_t msg_type, uint8_t flags)
{
    if (!linkit_frame_msg_type_valid(msg_type) || !linkit_frame_flags_valid(flags)) {
        return false;
    }

    if (msg_type == LINKIT_MSG_STREAM && (flags & LINKIT_FLAG_NEED_ACK) != 0U) {
        return false;
    }

    return true;
}

/**
 * @brief 根据 payload 长度计算完整线上帧长度。
 */
size_t linkit_frame_encoded_size(uint16_t payload_len)
{
    if (payload_len > LINKIT_FRAME_MAX_PAYLOAD) {
        return 0U;
    }

    return LINKIT_FRAME_HEADER_SIZE + payload_len + LINKIT_FRAME_CRC_SIZE;
}

/**
 * @brief 编码 v2.1 通用帧。
 * @return 成功返回写入字节数；失败返回 0。
 */
size_t linkit_frame_encode(const linkit_frame_t *frame, uint8_t *out, size_t cap)
{
    size_t total_len;
    uint16_t crc;

    if (frame == 0 || out == 0) {
        return 0U;
    }
    if (frame->payload_len > LINKIT_FRAME_MAX_PAYLOAD) {
        return 0U;
    }
    if (frame->payload_len > 0U && frame->payload == 0) {
        return 0U;
    }
    if (!linkit_frame_combo_valid(frame->msg_type, frame->flags)) {
        return 0U;
    }

    total_len = linkit_frame_encoded_size(frame->payload_len);
    if (cap < total_len) {
        return 0U;
    }

    out[0] = LINKIT_FRAME_HEAD;
    out[1] = LINKIT_FRAME_VERSION;
    out[2] = frame->module_id;
    out[3] = LINKIT_MAKE_TYPE_FLAGS(frame->msg_type, frame->flags);
    out[4] = frame->cmd_id;
    put_u16_le(&out[5], frame->seq);
    put_u16_le(&out[7], frame->payload_len);

    if (frame->payload_len > 0U) {
        memcpy(&out[LINKIT_FRAME_HEADER_SIZE], frame->payload, frame->payload_len);
    }

    crc = linkit_crc16_ccitt_false(out, LINKIT_FRAME_HEADER_SIZE + frame->payload_len);
    put_u16_le(&out[LINKIT_FRAME_HEADER_SIZE + frame->payload_len], crc);
    return total_len;
}

/**
 * @brief 解码 v2.1 通用帧并校验 CRC。
 * @param out_payload 指向 wire 缓冲区内部 payload 视图，不复制数据。
 */
linkit_frame_status_t linkit_frame_decode(const uint8_t *wire,
                                          size_t wire_len,
                                          linkit_frame_t *out_frame,
                                          const uint8_t **out_payload)
{
    uint16_t payload_len;
    size_t total_len;
    uint16_t expected_crc;
    uint16_t actual_crc;
    uint8_t type_flags;

    if (wire == 0 || out_frame == 0 || out_payload == 0) {
        return LINKIT_FRAME_ERR_ARG;
    }
    if (wire_len < (LINKIT_FRAME_HEADER_SIZE + LINKIT_FRAME_CRC_SIZE)) {
        return LINKIT_FRAME_ERR_ARG;
    }
    if (wire[0] != LINKIT_FRAME_HEAD) {
        return LINKIT_FRAME_ERR_BAD_HEAD;
    }
    if (wire[1] != LINKIT_FRAME_VERSION) {
        return LINKIT_FRAME_ERR_BAD_VERSION;
    }

    payload_len = get_u16_le(&wire[7]);
    if (payload_len > LINKIT_FRAME_MAX_PAYLOAD) {
        return LINKIT_FRAME_ERR_PAYLOAD_TOO_LARGE;
    }

    total_len = LINKIT_FRAME_HEADER_SIZE + payload_len + LINKIT_FRAME_CRC_SIZE;
    if (wire_len < total_len) {
        return LINKIT_FRAME_ERR_ARG;
    }

    type_flags = wire[3];
    if (!linkit_frame_msg_type_valid(LINKIT_GET_MSG_TYPE(type_flags))) {
        return LINKIT_FRAME_ERR_UNKNOWN_MSG_TYPE;
    }
    if (!linkit_frame_flags_valid(LINKIT_GET_FLAGS(type_flags))) {
        return LINKIT_FRAME_ERR_RESERVED_FLAGS;
    }
    if (!linkit_frame_combo_valid(LINKIT_GET_MSG_TYPE(type_flags), LINKIT_GET_FLAGS(type_flags))) {
        return LINKIT_FRAME_ERR_INVALID_FLAG_COMBO;
    }

    expected_crc = get_u16_le(&wire[LINKIT_FRAME_HEADER_SIZE + payload_len]);
    actual_crc = linkit_crc16_ccitt_false(wire, LINKIT_FRAME_HEADER_SIZE + payload_len);
    if (expected_crc != actual_crc) {
        return LINKIT_FRAME_ERR_BAD_CRC;
    }

    out_frame->module_id = wire[2];
    out_frame->msg_type = LINKIT_GET_MSG_TYPE(type_flags);
    out_frame->flags = LINKIT_GET_FLAGS(type_flags);
    out_frame->cmd_id = wire[4];
    out_frame->seq = get_u16_le(&wire[5]);
    out_frame->payload_len = payload_len;
    out_frame->payload = &wire[LINKIT_FRAME_HEADER_SIZE];
    *out_payload = out_frame->payload;
    return LINKIT_FRAME_OK;
}
