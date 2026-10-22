/**
 * @file linkit_state.c
 * @brief LinkIt 状态与帧解析核心逻辑
 *
 * @details
 * 本文件实现了 LinkIt 设备端（Host/Slave）关键状态处理函数，包括：
 * - POGO 插针检测去抖（linkit_det_get_stable_inserted）
 * - UART 高速模式数据帧解析器（linkit_frame_parser_feed）
 * - 业务帧完整性检查（linkit_frame_data_sane）
 * - 帧解析器重置（linkit_frame_parser_reset）
 *
 * 设计目标：在不额外依赖复杂协议栈、使用简单时序防抖与帧校验的前提下，
 * 提高链路可靠性和异常恢复能力。
 */

#include "linkit_core_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/**
 * @brief DET 去抖状态机
 * @param raw_inserted 当前检测到的 raw POGO 插入状态
 * @param now_ms 当前系统时间（毫秒）
 * @return 稳定的插针插入状态（true=已插入）
 * @note 需要持续调用，并由于静态变量只能在单线程或加锁环境中使用
 */
bool linkit_det_get_stable_inserted(bool raw_inserted, uint32_t now_ms)
{
    static uint32_t det_last_change_ms = 0;
    static bool det_stable_inserted = false;
    static bool det_prev_raw = false;
    static bool det_inited = false;

    if (!det_inited) {
        det_inited = true;
        det_prev_raw = raw_inserted;
        det_stable_inserted = raw_inserted;
        det_last_change_ms = now_ms;
    }

    if (raw_inserted != det_prev_raw) {
        det_prev_raw = raw_inserted;
        det_last_change_ms = now_ms;

        /* 拔出立即切断，避免悬空阶段继续误收数据。 */
        if (!raw_inserted && LINKIT_DET_REMOVE_DEBOUNCE_MS == 0U) {
            det_stable_inserted = false;
            return det_stable_inserted;
        }
    }

    if (!raw_inserted) {
        /* Remove debounce may be configured as 0 for "drop immediately".
         * Use the preprocessor branch so the 0 ms case does not leave behind
         * a noisy unsigned-comparison warning in open-source builds. */
#if LINKIT_DET_REMOVE_DEBOUNCE_MS == 0U
        det_stable_inserted = false;
#else
        if ((now_ms - det_last_change_ms) >= LINKIT_DET_REMOVE_DEBOUNCE_MS) {
            det_stable_inserted = false;
        }
#endif
    } else {
        if ((now_ms - det_last_change_ms) >= LINKIT_DET_INSERT_DEBOUNCE_MS) {
            det_stable_inserted = true;
        }
    }

    return det_stable_inserted;
}

typedef struct {
    uint8_t buf[sizeof(linkit_data_t)];
    uint8_t pos;
    bool in_frame;
    uint32_t last_byte_ms;
} linkit_rx_parser_t;

static linkit_rx_parser_t s_rx = {0};

/* 对业务帧做轻量范围检查，避免把噪声当成合法数据。 */
/**
 * @brief 业务帧范围检查
 * @param d 帧数据指针（可能是解码后的结构体）
 * @return true 数据在期望范围内
 */
bool linkit_frame_data_sane(const linkit_data_t *d)
{
    if (d == NULL) {
        return false;
    }
    if (d->header != LINKIT_FRAME_HEADER) return false;
    if (d->temperature < -40.0f || d->temperature > 125.0f) return false;
    if (d->humidity < 0.0f || d->humidity > 100.0f) return false;
    /*
     * pressure 在环境传感器里是气压，在正弦波/仪器测试里临时复用为频率等业务字段。
     * 0 是合法的“未填/无频率”值，不能因此丢弃整帧，否则有线 UART 会收不到正弦波帧。
     */
    if (d->pressure < 0.0f || d->pressure > 1200.0f) return false;
    return true;
}

/**
 * @brief 重置帧解析状态机
 * @details 在帧结束、超时或检测到错误时调用
 */
void linkit_frame_parser_reset(void)
{
    s_rx.pos = 0;
    s_rx.in_frame = false;
    s_rx.last_byte_ms = 0;
}

/**
 * @brief UART 高速模式下帧解析器数据输入接口
 * @param in 输入缓冲区（字节流）
 * @param n 输入字节数
 * @param now_ms 当前系统时间（毫秒），用于超时判定
 * @param out 解析成功时输出完整帧数据
 * @return true 输出有效帧，false 未完成或无效
 * @details
 * - 遇到 0xAA 字节作为帧头开始收包
 * - 连续收满 sizeof(linkit_data_t) 后做合法性检查
 * - 超时 30ms 丢弃半包并重置解析器
 */
bool linkit_frame_parser_feed(const uint8_t *in, size_t n, uint32_t now_ms, linkit_data_t *out)
{
    if (in == NULL || out == NULL) {
        return false;
    }

    if (s_rx.in_frame && (now_ms - s_rx.last_byte_ms > LINKIT_FRAME_GAP_TIMEOUT_MS)) {
        linkit_frame_parser_reset();
    }

    for (size_t i = 0; i < n; ++i) {
        uint8_t b = in[i];
        s_rx.last_byte_ms = now_ms;

        if (!s_rx.in_frame) {
            if (b == LINKIT_FRAME_HEADER) {
                s_rx.in_frame = true;
                s_rx.pos = 0;
                s_rx.buf[s_rx.pos++] = b;
            }
            continue;
        }

        s_rx.buf[s_rx.pos++] = b;

        if (s_rx.pos == sizeof(linkit_data_t)) {
            memcpy(out, s_rx.buf, sizeof(linkit_data_t));
            linkit_frame_parser_reset();
            if (linkit_frame_data_sane(out)) return true;
            continue;
        }

        if (s_rx.pos > sizeof(linkit_data_t)) {
            linkit_frame_parser_reset();
        }
    }

    return false;
}
