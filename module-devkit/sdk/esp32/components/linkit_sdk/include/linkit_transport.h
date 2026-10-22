/**
 * @file linkit_transport.h
 * @brief LinkIt 传输层接口定义
 *
 * @details
 * 本文件定义 transport 层对上提供的接口。
 * 当前稳定实现为 UART（握手与主链路），其他传输后续扩展。
 */
#ifndef LINKIT_TRANSPORT_H
#define LINKIT_TRANSPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "linkit_common.h"
#include "esp_now.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UART 是当前 stable SDK 的默认控制平面与主力有线链路。 */
int linkit_uart_open(uint32_t baud);
void linkit_uart_close(void);
int linkit_uart_set_baud(uint32_t baud);
int linkit_uart_read_exact(void *buf, size_t len, TickType_t ticks_to_wait);
int linkit_uart_read_bytes(void *buf, size_t len, TickType_t ticks_to_wait);
int linkit_uart_rx_backlog(void);
int linkit_uart_write_bytes(const void *buf, size_t len);
void linkit_uart_flush_input(void);
bool linkit_uart_is_online(void);

/* SPI beta 诊断探测（仅在 SPI 模式下生效）。 */
void linkit_spi_diag_probe_once_per_sec(void);

/* ESPNOW 无线接力传输层（用于 DET 断开后的兜底链路）。 */
int linkit_espnow_init(void);
void linkit_espnow_deinit(void);
void linkit_espnow_set_peer_mac(const uint8_t mac[6]);
int linkit_espnow_register_peer(const uint8_t mac[6]);
void linkit_espnow_set_rx_enabled(bool enabled);
bool linkit_espnow_try_get_data(linkit_data_t *out_data);
/* 从机侧双发路径：把当前数据帧发送到握手阶段登记的 Host peer。 */
int linkit_espnow_send_data(const linkit_data_t *data);

/* v2.1 变长 frame 经 ESP-NOW 收发（WIRELESS 兜底链路，单包 ≤ ESP_NOW_MAX_DATA_LEN_V2）。
 * send_frame 发送 wire bytes；try_get_frame 非阻塞取一帧到 out_wire（cap 为缓冲区容量）。 */
int linkit_espnow_send_frame(const uint8_t *wire, size_t len);
bool linkit_espnow_try_get_frame(uint8_t *out_wire, size_t cap, size_t *out_len);

/* 供 wireless_dap ESP-NOW recv 分发器回调：非 DAP 帧(CMW 前缀以外)转给 LinkIt on_data_recv。
 * 让 LinkIt 与 wireless_dap 共享同一 ESP-NOW 实例（单回调注册模型）。 */
void linkit_espnow_deliver_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len);

#ifdef __cplusplus
}
#endif

#endif
