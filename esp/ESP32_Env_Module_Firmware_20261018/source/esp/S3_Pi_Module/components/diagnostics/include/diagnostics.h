/**
 * @file diagnostics.h
 * @brief 诊断日志与链路可观测接口
 *
 * @details
 * 统一管理项目中的调试输出开关与诊断函数，避免业务代码散落日志细节。
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 开机 I2C 扫描诊断开关。 */
#define DIAG_I2C_SCAN_ON_BOOT 0

/* UI 触控日志诊断开关：置 1 时输出每次触控采样坐标，置 0 时静默。 */
#define DIAG_UI_TOUCH_LOG 0

/**
 * @brief 扫描触控 I2C 总线并输出在线地址
 * @param tag 日志模块标签
 */
void diagnostics_run_i2c_scan_touch_bus(const char *tag);

/**
 * @brief 输出 UI 触控采样日志（内部带节流）
 * @param tag 日志模块标签
 * @param x   触控坐标 X
 * @param y   触控坐标 Y
 * @param pressed 是否按下
 * @param hit_disc 是否命中 DISC 按钮区域
 */
void diagnostics_log_touch_sample(const char *tag, int16_t x, int16_t y, bool pressed, bool hit_disc);

/**
 * @brief 输出 DISC 按钮点击日志
 * @param tag 日志模块标签
 */
void diagnostics_log_disconnect_click(const char *tag);

/**
 * @brief 输出链路状态机迁移日志
 * @param tag 日志模块标签
 * @param from_state 迁移前状态
 * @param event 触发事件
 * @param to_state 迁移后状态
 * @param detail 附加细节
 */
void diagnostics_log_link_fsm(const char *tag,
                              const char *from_state,
                              const char *event,
                              const char *to_state,
                              const char *detail);

#ifdef __cplusplus
}
#endif

