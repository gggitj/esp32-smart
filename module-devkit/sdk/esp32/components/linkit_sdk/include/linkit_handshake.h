#ifndef LINKIT_HANDSHAKE_H
#define LINKIT_HANDSHAKE_H

#include "linkit_config.h"
#include "linkit_protocol.h"
#include "linkit_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LINKIT_HS_QUERY        LINKIT_HANDSHAKE_QUERY
#define LINKIT_HS_QUERY_LEN    LINKIT_HANDSHAKE_QUERY_LEN
#define LINKIT_HS_MAGIC_LNK1   LINKIT_MAGIC
#define LINKIT_HS_MAGIC_LEN    LINKIT_MAGIC_LEN
#define LINKIT_HS_ACK          LINKIT_HANDSHAKE_ACK
#define LINKIT_HS_ACK_LEN      LINKIT_HANDSHAKE_ACK_LEN

#define LINKIT_PROTOCOL_ID_UART_HIGH     0x01u
#define LINKIT_PROTOCOL_ID_SPI_RESERVED  0x02u
#define LINKIT_PROTOCOL_ID_I2C_RESERVED  0x03u

#define LINKIT_CORE_CMD_INFO 0x00u

/*
 * 本头文件只描述 LinkIt v2.2 的公开握手契约：
 * - S2 低速握手仍保持 WHO? / LNK1 / OK! 主线；
 * - 协议时序、UART、DET、ESP-NOW 等可调参数由 linkit_config.h 中的
 *   linkit_protocol_config_t / linkit_host_config_t / linkit_device_config_t 提供；
 * - 具体 S1-S5 状态机函数属于内部实现，声明在 src/internal/linkit_hs_internal.h。
 */

typedef struct __attribute__((packed)) {
    uint8_t magic[4];
    char module_type[LINKIT_MODULE_TYPE_LEN];
    uint8_t mac[6];
    uint8_t protocol_id;
} linkit_lnk1_packet_t;

typedef struct __attribute__((packed)) {
    uint8_t ack[3];
    uint8_t host_mac[6];
} linkit_ok_packet_t;

#ifdef __cplusplus
}
#endif

#endif
