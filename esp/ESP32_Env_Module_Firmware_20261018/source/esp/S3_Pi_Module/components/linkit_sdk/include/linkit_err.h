/**
 * @file linkit_err.h
 * @brief LinkIt SDK 专用错误码
 *
 * @details
 * LinkIt 公开 API 统一返回 esp_err_t。若当前 ESP-IDF 版本提供
 * ESP_ERR_USER_BASE，则基于该官方用户区分配；否则使用本 SDK 的
 * 兼容基准值并保留独立子段。
 */
#ifndef LINKIT_ERR_H
#define LINKIT_ERR_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef ESP_ERR_USER_BASE
/* ESP-IDF 5.5.x 尚未定义 ESP_ERR_USER_BASE；0xA000 作为 LinkIt 的兼容用户区基准。 */
#define ESP_ERR_USER_BASE                 0xA000
#endif

#define ESP_ERR_LINKIT_BASE               (ESP_ERR_USER_BASE + 0x1000)
#define ESP_ERR_LINKIT_NOT_INSTALLED      (ESP_ERR_LINKIT_BASE + 1)
#define ESP_ERR_LINKIT_ALREADY_INSTALLED  (ESP_ERR_LINKIT_BASE + 2)
#define ESP_ERR_LINKIT_NOT_LINKED         (ESP_ERR_LINKIT_BASE + 3)
#define ESP_ERR_LINKIT_HS_TIMEOUT         (ESP_ERR_LINKIT_BASE + 4)
#define ESP_ERR_LINKIT_BAD_LNK1           (ESP_ERR_LINKIT_BASE + 5)
#define ESP_ERR_LINKIT_LINK_DOWN          (ESP_ERR_LINKIT_BASE + 6)
#define ESP_ERR_LINKIT_FRAME_BAD_CRC      (ESP_ERR_LINKIT_BASE + 7)
#define ESP_ERR_LINKIT_PEER_NAK           (ESP_ERR_LINKIT_BASE + 8)
#define ESP_ERR_LINKIT_NO_DATA            (ESP_ERR_LINKIT_BASE + 9)
#define ESP_ERR_LINKIT_MAX                (ESP_ERR_LINKIT_BASE + 0x1000)

/* LinkIt 错误码占用区间: [ESP_ERR_LINKIT_BASE, ESP_ERR_LINKIT_BASE + 0x1000) */

#ifdef __cplusplus
}
#endif

#endif
