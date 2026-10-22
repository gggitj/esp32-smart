/**
 * @file linkit_device.h
 * @brief LinkIt Device legacy 兼容头
 *
 * @details
 * 新工程优先 include `linkit.h` 并使用 `linkit_device_install()`。
 * 本文件保留旧 `linkit_slave_*` 声明，避免早期 Device 侧工程迁移时
 * 一次性改动过大。
 */
#ifndef LINKIT_DEVICE_H
#define LINKIT_DEVICE_H

#include "linkit_common.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @deprecated 旧 Device 初始化入口；新工程请使用 linkit_device_install()。
 */
void linkit_slave_init(const char *module_type);

/**
 * @deprecated 更新 Device 当前待发送的 legacy 数据。
 */
void linkit_slave_set_data(const linkit_data_t *data);

/**
 * @deprecated 查询 Device 是否已经进入高速有线发送态。
 */
bool linkit_slave_is_wired(void);

/**
 * @deprecated 查询 Device 是否已经通过 ACK 获得 Host MAC。
 */
bool linkit_slave_has_host_mac(void);

#ifdef __cplusplus
}
#endif

#endif
