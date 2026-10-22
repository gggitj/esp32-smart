/**
 * @file linkit_role_host.c
 * @brief LinkIt Host 角色预留文件
 *
 * @details
 * v2.2 当前 Host 主流程仍集中在 core/linkit_core.c，便于兼容旧 demo。
 * 后续拆分 S1-S5 状态机时，Host 侧接入探测、低速握手、高速切换和
 * 双模接收策略会逐步迁移到本文件。
 */

#include "linkit.h"
