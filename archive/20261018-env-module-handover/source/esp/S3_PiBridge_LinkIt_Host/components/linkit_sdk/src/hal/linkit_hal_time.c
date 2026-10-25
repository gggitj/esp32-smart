/**
 * @file linkit_hal_time.c
 * @brief LinkIt 时间 HAL 预留层
 *
 * @details
 * 当前实现直接使用 FreeRTOS tick。后续如需移植到不同运行环境，可在此
 * 统一封装毫秒计时、超时比较和延时接口。
 */

#include "linkit.h"
