/**
 * @file linkit_det.h
 * @brief LinkIt DET 引脚子系统公开契约。
 *
 * @details
 * DET 是五引脚接口里的接入检测脚。v2.2 默认把它作为插拔检测和链路自愈触发源；
 * 后续如果启用三线 SPI 预留路径，DET 可被复用为其中一条通信信号线。
 *
 * 本文件只放公开类型和 API 声明，不包含 GPIO 细节。具体电平读取、去抖和中断处理
 * 由 SDK 内部 HAL/状态机实现。
 */
#ifndef LINKIT_DET_H
#define LINKIT_DET_H

#include "linkit_config.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief DET 事件类型。
 *
 * @note v2.2 的电平语义按队内技术方案统一：
 * 高到低表示插入，低到高表示拔出。插入需要去抖，拔出优先级最高。
 */
typedef enum {
    LINKIT_DET_EVT_INSERT = 0,
    LINKIT_DET_EVT_EJECT,
} linkit_det_event_t;

/**
 * @brief DET 事件回调。
 *
 * @param event DET 插入/拔出事件。
 * @param user 用户注册时传入的上下文指针。
 */
typedef void (*linkit_det_cb_t)(linkit_det_event_t event, void *user);

/**
 * @brief 安装 DET 子系统。
 *
 * @details 当前 P1 只冻结 API 形态，后续实现会由 linkit_host_install() /
 * linkit_device_install() 间接调用。应用层通常不需要直接调用本函数。
 */
esp_err_t linkit_det_install(const linkit_det_config_t *config);

/**
 * @brief 卸载 DET 子系统。
 */
esp_err_t linkit_det_uninstall(void);

/**
 * @brief 注册 DET 事件回调。
 */
esp_err_t linkit_det_register_cb(linkit_det_cb_t cb, void *user);

/**
 * @brief 获取当前 DET 原始电平。
 *
 * @param out_level 输出 GPIO 电平，0 或 1。
 */
esp_err_t linkit_det_get_level(int *out_level);

/**
 * @brief 切换 DET 运行时角色。
 *
 * @note v2.2 默认只支持 LINKIT_DET_ROLE_DETECT。SPI3W 复用角色属于预留路径，
 * 未启用时应返回 ESP_ERR_NOT_SUPPORTED。
 */
esp_err_t linkit_det_set_role(linkit_det_role_t role);

#ifdef __cplusplus
}
#endif

#endif /* LINKIT_DET_H */
