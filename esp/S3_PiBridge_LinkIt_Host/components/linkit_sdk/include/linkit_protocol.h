/**
 * @file linkit_protocol.h
 * @brief LinkIt 协议层定义
 *
 * @details
 * 本文件存放专利链路层常量与 legacy 结构：
 * - 握手魔数与默认波特率
 * - 握手帧与 ACK 帧
 * - 旧版业务 payload 定义
 *
 * @note
 * v2.1 起新的通用数据帧定义放在 linkit_frame.h：
 * 0xAA + version + module_id + type_flags + cmd_id + seq + payload_len
 * + payload + crc16。这里保留的 linkit_data_t 仅用于兼容 v2.0 演示链路。
 */
#ifndef LINKIT_PROTOCOL_H
#define LINKIT_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* LinkIt 固定握手魔数，主机通过它识别合法模块。 */
#define LINKIT_MAGIC      "LNK1"
#define LINKIT_MAGIC_LEN  4

/* 当前稳定版采用 115200 低速握手，握手成功后切到 2 Mbps。 */
#define LINKIT_BAUD_LOW   115200
#define LINKIT_BAUD_HIGH  2000000

/* 专利交底书定义的低速握手字串。 */
#define LINKIT_HANDSHAKE_QUERY      "WHO?"
#define LINKIT_HANDSHAKE_QUERY_LEN  4
#define LINKIT_HANDSHAKE_ACK        "OK!"
#define LINKIT_HANDSHAKE_ACK_LEN    3

/* 帧头与时序参数。 */
#define LINKIT_FRAME_HEADER                 0xAA
/* 主机单次读取握手包的窗口。适当放宽可降低首包刚好跨调度片时的超时概率。 */
#define LINKIT_HOST_HANDSHAKE_READ_MS       150U
#define LINKIT_HANDSHAKE_RETRY_COOLDOWN_MS  500U
#define LINKIT_HANDSHAKE_SWITCH_DELAY_MS    180U
/* 从机在等待 ACK 时的总窗口与握手包重发节奏。 */
#define LINKIT_SLAVE_ACK_TIMEOUT_MS         1000U
#define LINKIT_SLAVE_INFO_RESEND_MS         120U
#define LINKIT_FRAME_GAP_TIMEOUT_MS         30U
/* Wired RX timeout should be comfortably larger than one normal publish period.
 * BME680-class telemetry often updates around 1 Hz, so 2.5 s avoids false
 * rollbacks while still recovering quickly from a real wired stall. */
#define LINKIT_WIRED_RX_TIMEOUT_MS          2500U

/* 从机在 2 Mbps 有线态下统计 Host RX 活动。
 * 窗口内累计达到阈值，才认为 Host 已重启并开始重发 WHO?，随后回退到 115200。
 * 注意：从机高速发送业务数据时，TX 会对相邻 RX 产生串扰（实测每 200ms 约 10+ 字节），
 * 阈值过低会在数据流恢复瞬间被串扰误触发回退、把链路打断。这里取 64 字节：
 * 串扰量级远达不到，而 Host 真在 115200 反复发 WHO? 时（每 200ms 上百字节乱码）能可靠命中。 */
#define LINKIT_WIRED_RX_BREAK_WINDOW_MS     200U
#define LINKIT_WIRED_RX_BREAK_BYTE_THRES    64U

/* 有线保活：主机在 WIRED 态周期下发，模块用它判断链路是否还活着。
 * 拔出检测原本只靠 RX 噪声突发（概率性、会漏检），这是确定性兜底。 */
#define LINKIT_WIRED_KEEPALIVE_PERIOD_MS   200U
#define LINKIT_WIRED_RX_SILENCE_TIMEOUT_MS 800U   /* 允许连丢 3 个 keepalive */
#define LINKIT_CMD_KEEPALIVE               0x7EU  /* 与现有 cmd_id 不冲突（instrument 0x10~0x21，thermal 0x54/0x55） */

/* 保留给链路同步/调试使用的同步字节。 */
#define LINKIT_SYNC_BYTE  0x55

#define LINKIT_MODULE_TYPE_LEN 16

/* DET 非对称去抖策略：
 * - 复杂供电/浮地环境下，短暂跌落不应立刻把链路判死
 * - 插入确认仍保持较短，保证重连体感 */
#define LINKIT_DET_REMOVE_DEBOUNCE_MS 1000U
#define LINKIT_DET_INSERT_DEBOUNCE_MS 60U

/* 设备在 115200 握手阶段返回的基础信息。 */
typedef struct {
    /* 前 4 字节为 LINKIT_MAGIC，后 4 字节预留给后续扩展。 */
    char magic[8];
    char module_type[LINKIT_MODULE_TYPE_LEN];
    uint8_t mac_addr[6];
} __attribute__((packed)) linkit_handshake_t;

/* 主机确认握手后返回 ACK，并附带自身 MAC。 */
typedef struct {
    char status[4];
    uint8_t host_mac[6];
} __attribute__((packed)) linkit_host_ack_t;

/* Legacy payload kept for the v2.0 sine-wave/telemetry demo path. */
typedef struct {
    uint8_t header;
    uint32_t timestamp;
    float temperature;
    float humidity;
    float pressure;
    float gas_resistance_ohms;
} __attribute__((packed)) linkit_data_t;

#ifdef __cplusplus
}
#endif

#endif
