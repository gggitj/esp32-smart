idf_component_register(
    SRCS
        "src/core/linkit_core.c"
        "src/core/linkit_crc16.c"
        "src/core/linkit_frame.c"
        "src/core/linkit_handshake.c"
        "src/core/linkit_parser.c"
        "src/core/linkit_state.c"
        "src/hal/linkit_hal_board.c"
        "src/hal/linkit_hal_gpio.c"
        "src/hal/linkit_hal_time.c"
        "src/roles/linkit_role_host.c"
        "src/roles/linkit_role_device.c"
        "src/transport/linkit_transport_uart.c"
        "src/transport/linkit_transport_i2c.c"
        "src/transport/linkit_transport_spi.c"
        "src/transport/linkit_transport_espnow.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src/internal"
    REQUIRES board_pins esp_wifi esp_event nvs_flash driver esp_hw_support freertos
    PRIV_REQUIRES diagnostics
)

# GCC 14 (esp-14.2.0) 在 -Werror 下对 strncpy(dst,src,sizeof(dst)-1)（后接 NUL 终止）
# 误报 -Werror=stringop-truncation。该用法在 linkit_sdk 中是安全且有意的（缓冲已先清零，
# 拷贝 size-1 字节保留 NUL 位）。此处仅关闭该特定告警，不修改 protocols 源码逻辑。
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-stringop-truncation)
