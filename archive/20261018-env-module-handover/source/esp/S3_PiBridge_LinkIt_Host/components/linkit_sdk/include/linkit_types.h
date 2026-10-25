/**
 * @file linkit_types.h
 * @brief LinkIt 公共类型定义
 *
 * @details
 * 本文件用于放置跨模块共享的基础类型，
 * 当前主要包含运行时链路状态枚举。
 */
#ifndef LINKIT_TYPES_H
#define LINKIT_TYPES_H

#include "linkit_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LINKIT_STATE_DISCONNECTED = 0,
    LINKIT_STATE_HANDSHAKING,
    LINKIT_STATE_WIRED,
    LINKIT_STATE_WIRELESS,
} linkit_state_t;

typedef enum {
    LINKIT_FLOW_S0_DISCONNECTED = 0,
    LINKIT_FLOW_S1_ATTACH_DETECT,
    LINKIT_FLOW_S2_LOW_SPEED_HANDSHAKE,
    LINKIT_FLOW_S3_HIGH_SPEED_SWITCH,
    LINKIT_FLOW_S4_DUAL_MODE_RUNTIME,
    LINKIT_FLOW_S5_LINK_RECOVERY,
} linkit_flow_state_t;

typedef enum {
    LINKIT_ESPNOW_STATE_DISABLED = 0,
    LINKIT_ESPNOW_STATE_IDLE,
    LINKIT_ESPNOW_STATE_PEERED,
    LINKIT_ESPNOW_STATE_HOT_STANDBY,
    LINKIT_ESPNOW_STATE_ACTIVE_FALLBACK,
} linkit_espnow_state_t;

typedef enum {
    LINKIT_HS_NAK_NONE             = 0x00,
    LINKIT_HS_NAK_VERSION_MISMATCH = 0x01,
    LINKIT_HS_NAK_FEATURE_MISSING  = 0x02,
    LINKIT_HS_NAK_MTU_TOO_SMALL    = 0x03,
    LINKIT_HS_NAK_BAD_EXT_DESC     = 0x04,
    LINKIT_HS_NAK_INTERNAL         = 0xFF,
} linkit_hs_nak_t;

typedef enum {
    LINKIT_RECOVER_FRAME_LEVEL = 0,
    LINKIT_RECOVER_LINK_LEVEL,
    LINKIT_RECOVER_HANDSHAKE_LEVEL,
} linkit_recover_level_t;

typedef enum {
    LINKIT_MOD_CORE       = 0x00,
    LINKIT_MOD_ENV_LEGACY = 0x01, /* Deprecated, planned removal in v3.0 */
    LINKIT_MOD_ENV        = 0x02,
    LINKIT_MOD_SCOPE      = 0x03,
    LINKIT_MOD_LA         = 0x04,
    LINKIT_MOD_THERMAL    = 0x05,
    LINKIT_MOD_PMETER     = 0x06,
    LINKIT_MOD_INSTRUMENT = 0x20,
    LINKIT_MOD_BROADCAST  = 0xFF,
} linkit_module_id_t;

typedef enum {
    LINKIT_CMD_INFO   = 0x00,
    LINKIT_CMD_START  = 0x01,
    LINKIT_CMD_STOP   = 0x02,
    LINKIT_CMD_CONFIG = 0x03,
    LINKIT_CMD_STATUS = 0x04,
    LINKIT_CMD_RESET  = 0x05,
} linkit_common_cmd_t;

#define LINKIT_MUX_UART_DEFAULT   (1u << 0)
#define LINKIT_MUX_I2C_RESERVED   (1u << 1)
#define LINKIT_MUX_SPI3W_RESERVED (1u << 2)

#define LINKIT_FC_FRAME_V21   (1u << 0)
#define LINKIT_FC_LEGACY_DATA (1u << 1)
#define LINKIT_FC_STREAM      (1u << 2)
#define LINKIT_FC_REQ_RSP     (1u << 3)
#define LINKIT_FC_EVT         (1u << 4)
#define LINKIT_FC_ACK_NAK     (1u << 5)
#define LINKIT_FC_CTRL        (1u << 6)

typedef struct __attribute__((packed)) {
    uint8_t ext_version;
    uint8_t module_id;
    uint16_t frame_proto_min;
    uint16_t frame_proto_max;
    uint16_t mtu_uart;
    uint16_t mtu_espnow;
    uint32_t uart_high_baud;
    uint16_t pin_mux_caps;
    uint16_t feature_caps;
    uint8_t reserved[6];
} linkit_ext_desc_t;

typedef struct __attribute__((packed)) {
    uint8_t ext_version;
    uint8_t result;
    uint16_t frame_proto;
    uint16_t mtu;
    uint16_t enabled_features;
    uint8_t reserved[6];
} linkit_ext_session_t;

typedef struct {
    linkit_flow_state_t wired_state;
    linkit_espnow_state_t wireless_state;
    bool consuming_wireless;
    uint8_t peer_mac[6];
    char module_type[16];
    uint8_t module_id;
    uint16_t active_frame_proto;
    uint16_t active_mtu;
    uint16_t enabled_features;
    uint16_t pin_mux_active;
    uint32_t hs_attempts;
    uint32_t frame_recovers;
    uint32_t link_recovers;
    uint32_t handshake_recovers;
    linkit_hs_nak_t last_peer_nak;
} linkit_link_status_t;

#ifdef __cplusplus
}
#endif

#endif
