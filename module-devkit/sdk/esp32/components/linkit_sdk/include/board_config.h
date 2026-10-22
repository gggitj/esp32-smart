/**
 * @file board_config.h
 * @brief 板级配置兼容头
 *
 * @details
 * 本文件用于兼容旧代码的 include 入口。
 * 目前仅聚合 board_pins.h 与 linkit_pins.h，避免上层重复改动。
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// LEGACY wrapper, migrate includes to:
// - board_pins.h (board-level macros)
// - linkit_pins.h (linkit pogo macros)

#include "board_pins.h"
#include "linkit_pins.h"

#endif
