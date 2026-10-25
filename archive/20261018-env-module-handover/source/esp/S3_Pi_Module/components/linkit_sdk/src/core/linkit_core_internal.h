/**
 * @file linkit_core_internal.h
 * @brief LinkIt core 子模块私有接口声明
 *
 * @details
 * 本头文件仅供 src/core 与 src/roles 内部使用，不属于公开 API。
 * 它集中声明低速握手、DET 去抖、legacy 帧解析和 Device 角色内部函数，
 * 避免在多个 .c 文件里散落临时 extern 声明。
 */
#ifndef LINKIT_CORE_INTERNAL_H
#define LINKIT_CORE_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "linkit_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 执行一次 Host 侧低速握手尝试。
 * @details 流程为 WHO? -> LNK1 握手包 -> OK! ACK。
 */
bool linkit_try_handshake_once(uint8_t out_peer_mac[6], char out_module_type[LINKIT_MODULE_TYPE_LEN]);

/**
 * @brief DET 插入状态去抖。
 * @return true 表示已经确认稳定插入。
 */
bool linkit_det_get_stable_inserted(bool raw_inserted, uint32_t now_ms);

/**
 * @brief 检查 legacy linkit_data_t 是否像一帧有效业务数据。
 */
bool linkit_frame_data_sane(const linkit_data_t *d);

/** @brief 重置 legacy 固定长度帧解析器。 */
void linkit_frame_parser_reset(void);

/**
 * @brief 向 legacy 固定长度解析器输入一段 UART 字节。
 * @return true 表示解析出一帧完整业务数据。
 */
bool linkit_frame_parser_feed(const uint8_t *in, size_t n, uint32_t now_ms, linkit_data_t *out);

bool linkit_v21_feed_rx_bytes(const uint8_t *data, size_t len, uint32_t now_ms);
void linkit_v21_rx_tick(uint32_t now_ms);

/** @brief 初始化 Device 角色内部状态机。 */
void linkit_device_init_internal(void);

/** @brief 设置 Device 是否由 SDK 初始化并使用 ESP-NOW。 */
void linkit_device_set_espnow_enabled_internal(bool enabled);

/** @brief 推进 Device 角色状态机一步。 */
void linkit_device_process_internal(void);

/** @brief 查询 Device 角色当前链路状态。 */
linkit_state_t linkit_device_get_state_internal(void);

/** @brief 设置 Device 低速握手阶段上报的模块类型。 */
void linkit_device_set_module_type_internal(const char *module_type);

/** @brief 更新 Device 当前待发送的 legacy 业务数据。 */
void linkit_device_set_data_internal(const linkit_data_t *data);

/** @brief 查询 Device 是否已经进入高速有线态。 */
bool linkit_device_is_wired_internal(void);

/** @brief 查询 Device 是否已经从 OK! ACK 中获得 Host MAC。 */
bool linkit_device_has_host_mac_internal(void);

#ifdef __cplusplus
}
#endif

#endif
