#ifndef LINKIT_CONFIG_H
#define LINKIT_CONFIG_H

#include "driver/uart.h"
#include "esp_wifi_types.h"
#include "linkit_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Default timing values from the patent embodiment 1.
 * The protocol does not lock these values. Callers may override any field
 * via linkit_timing_t at runtime; the defaults exist only to match the
 * embodiment and to ease bring-up.
 */
#define LINKIT_DEFAULT_BAUD_LOW              115200u
#define LINKIT_DEFAULT_BAUD_HIGH             2000000u
#define LINKIT_DEFAULT_RATE_SWITCH_DELAY_MS  180u
#define LINKIT_DEFAULT_HANDSHAKE_TIMEOUT_MS  500u
#define LINKIT_DEFAULT_HANDSHAKE_COOLDOWN_MS 500u
#define LINKIT_DEFAULT_WIRED_TIMEOUT_MS      1000u
#define LINKIT_DEFAULT_FRAME_GAP_TIMEOUT_MS  30u
#define LINKIT_DEFAULT_SYNC_PADDING_BYTE     0x55u
#define LINKIT_DEFAULT_SYNC_PADDING_COUNT    32u

#define LINKIT_DEFAULT_DET_INSERT_DEBOUNCE_MS 30u
#define LINKIT_DEFAULT_DET_REMOVE_DEBOUNCE_MS 0u
#define LINKIT_DEFAULT_DET_ALLOW_SPI_REMAP    false

/* Allowed wired timeout band per feedback note 7 (e.g. 1000~2500 ms). */
#define LINKIT_WIRED_TIMEOUT_MIN_MS 1000u
#define LINKIT_WIRED_TIMEOUT_MAX_MS 2500u

typedef struct {
    uint32_t baud_low;
    uint32_t baud_high;
    uint16_t rate_switch_delay_ms;
    uint16_t handshake_timeout_ms;
    uint16_t handshake_cooldown_ms;
    uint16_t wired_timeout_ms;
    uint16_t frame_gap_timeout_ms;
    uint8_t  sync_padding_byte;
    uint8_t  sync_padding_count;
} linkit_timing_t;

typedef enum {
    LINKIT_DET_ROLE_DETECT = 0,
    LINKIT_DET_ROLE_SPI3W_CS_RESERVED,
    LINKIT_DET_ROLE_SPI3W_DIR_RESERVED,
    LINKIT_DET_ROLE_SPI3W_SIGNAL_RESERVED,
} linkit_det_role_t;

typedef struct {
    uint16_t insert_debounce_ms;
    uint16_t remove_debounce_ms;
    bool     allow_spi_remap;
    /*
     * 当前版本尚未应用该字段；DET 引脚由 board_pins/linkit_pins.h 中的
     * POGO_PIN_DET 决定。换板时请修改 linkit_pins.h。
     */
    int      gpio_num;
    bool     host_pullup;
    uint16_t cooldown_ms;
    bool     enable_runtime_reuse;
    linkit_det_role_t runtime_role;
} linkit_det_config_t;

typedef enum {
    LINKIT_ESPNOW_OWNERSHIP_DISABLED = 0,
    LINKIT_ESPNOW_OWNED_BY_SDK,
    LINKIT_ESPNOW_OWNED_BY_APP,
} linkit_espnow_ownership_t;

typedef struct {
    /*
     * 当前版本尚未应用 uart_port/tx_gpio/rx_gpio；UART 端口和引脚由
     * board_pins/linkit_pins.h 中的 POGO_UART_NUM/POGO_PIN_TX/POGO_PIN_RX 决定。
     * 换板时请修改 linkit_pins.h。
     */
    uart_port_t uart_port;
    int tx_gpio;
    int rx_gpio;
    int rx_buffer_size;
    int tx_buffer_size;
} linkit_uart_config_t;

typedef struct {
    uint32_t low_baud;
    uint32_t high_baud;
    uint32_t switch_delay_ms;
    uint32_t sync_pad_bytes;
    uint8_t sync_pad_value;
    uint32_t handshake_who_timeout_ms;
    uint32_t handshake_cooldown_ms;
    uint32_t wired_rx_silence_ms;
    uint32_t frame_interbyte_timeout_ms;
} linkit_protocol_config_t;

typedef struct {
    linkit_espnow_ownership_t ownership;
    uint8_t channel;
    wifi_interface_t ifx;
    int send_max_retry;
} linkit_espnow_config_t;

typedef struct {
    linkit_uart_config_t uart;
    linkit_protocol_config_t protocol;
    linkit_det_config_t det;
    linkit_espnow_config_t espnow;
    bool enable_spi3w_reserved;
} linkit_host_config_t;

typedef struct {
    linkit_uart_config_t uart;
    linkit_protocol_config_t protocol;
    linkit_det_config_t det;
    linkit_espnow_config_t espnow;
    const linkit_ext_desc_t *device_ext_desc;
} linkit_device_config_t;

#define LINKIT_UART_CONFIG_DEFAULT() (linkit_uart_config_t) { \
    .uart_port = UART_NUM_1,                                  \
    .tx_gpio = -1,                                            \
    .rx_gpio = -1,                                            \
    .rx_buffer_size = 4096,                                   \
    .tx_buffer_size = 1024,                                   \
}

#define LINKIT_PROTOCOL_CONFIG_DEFAULT() (linkit_protocol_config_t) { \
    .low_baud = 0,                                                    \
    .high_baud = 0,                                                   \
    .switch_delay_ms = 0,                                             \
    .sync_pad_bytes = 0,                                              \
    .sync_pad_value = 0,                                              \
    .handshake_who_timeout_ms = 0,                                    \
    .handshake_cooldown_ms = 0,                                       \
    .wired_rx_silence_ms = 0,                                         \
    .frame_interbyte_timeout_ms = 0,                                  \
}

#define LINKIT_DET_CONFIG_DEFAULT() (linkit_det_config_t) { \
    .insert_debounce_ms = 0,                                \
    .remove_debounce_ms = 0,                                \
    .allow_spi_remap = false,                               \
    .gpio_num = -1,                                         \
    .host_pullup = true,                                    \
    .cooldown_ms = 0,                                       \
    .enable_runtime_reuse = false,                          \
    .runtime_role = LINKIT_DET_ROLE_DETECT,                 \
}

#define LINKIT_ESPNOW_CONFIG_DEFAULT() (linkit_espnow_config_t) { \
    .ownership = LINKIT_ESPNOW_OWNERSHIP_DISABLED,                \
    .channel = 1,                                                 \
    .ifx = WIFI_IF_STA,                                           \
    .send_max_retry = 3,                                          \
}

#define LINKIT_HOST_CONFIG_DEFAULT() (linkit_host_config_t) { \
    .uart = LINKIT_UART_CONFIG_DEFAULT(),                     \
    .protocol = LINKIT_PROTOCOL_CONFIG_DEFAULT(),             \
    .det = LINKIT_DET_CONFIG_DEFAULT(),                       \
    .espnow = LINKIT_ESPNOW_CONFIG_DEFAULT(),                 \
    .enable_spi3w_reserved = false,                           \
}

#define LINKIT_DEVICE_CONFIG_DEFAULT() (linkit_device_config_t) { \
    .uart = LINKIT_UART_CONFIG_DEFAULT(),                         \
    .protocol = LINKIT_PROTOCOL_CONFIG_DEFAULT(),                 \
    .det = LINKIT_DET_CONFIG_DEFAULT(),                           \
    .espnow = LINKIT_ESPNOW_CONFIG_DEFAULT(),                     \
    .device_ext_desc = 0,                                         \
}

void linkit_timing_set_defaults(linkit_timing_t *out);
void linkit_det_config_set_defaults(linkit_det_config_t *out);

#ifdef __cplusplus
}
#endif

#endif
