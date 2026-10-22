/**
 * @file linkit_config.c
 * @brief LinkIt 默认配置填充函数
 *
 * @details
 * 本文件只负责把协议默认值写入配置结构体，不负责安装驱动或修改运行状态。
 * 默认值来自 v2.2 技术方案和专利实施例，应用层仍可在 install 前覆盖字段。
 */

#include "linkit_config.h"

/**
 * @brief 填充 LinkIt 时序默认值。
 * @param out 输出配置结构体，允许为 NULL。
 */
void linkit_timing_set_defaults(linkit_timing_t *out)
{
    if (out == 0) {
        return;
    }
    out->baud_low              = LINKIT_DEFAULT_BAUD_LOW;
    out->baud_high             = LINKIT_DEFAULT_BAUD_HIGH;
    out->rate_switch_delay_ms  = LINKIT_DEFAULT_RATE_SWITCH_DELAY_MS;
    out->handshake_timeout_ms  = LINKIT_DEFAULT_HANDSHAKE_TIMEOUT_MS;
    out->handshake_cooldown_ms = LINKIT_DEFAULT_HANDSHAKE_COOLDOWN_MS;
    out->wired_timeout_ms      = LINKIT_DEFAULT_WIRED_TIMEOUT_MS;
    out->frame_gap_timeout_ms  = LINKIT_DEFAULT_FRAME_GAP_TIMEOUT_MS;
    out->sync_padding_byte     = LINKIT_DEFAULT_SYNC_PADDING_BYTE;
    out->sync_padding_count    = LINKIT_DEFAULT_SYNC_PADDING_COUNT;
}

/**
 * @brief 填充 DET 引脚默认配置。
 * @param out 输出配置结构体，允许为 NULL。
 */
void linkit_det_config_set_defaults(linkit_det_config_t *out)
{
    if (out == 0) {
        return;
    }
    out->insert_debounce_ms = LINKIT_DEFAULT_DET_INSERT_DEBOUNCE_MS;
    out->remove_debounce_ms = LINKIT_DEFAULT_DET_REMOVE_DEBOUNCE_MS;
    out->allow_spi_remap    = LINKIT_DEFAULT_DET_ALLOW_SPI_REMAP;
    out->gpio_num           = -1;
    out->host_pullup        = true;
    out->cooldown_ms        = LINKIT_DEFAULT_HANDSHAKE_COOLDOWN_MS;
    out->enable_runtime_reuse = false;
    out->runtime_role       = LINKIT_DET_ROLE_DETECT;
}
