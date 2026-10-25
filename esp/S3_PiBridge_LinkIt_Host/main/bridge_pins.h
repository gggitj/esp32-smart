#pragma once

#include <stdint.h>
#include "sdkconfig.h"
#include "linkit_pins.h"

#define BRIDGE_UART_TX_GPIO 43
#define BRIDGE_UART_RX_GPIO 44

/* Reserved only: no battery driver or I2C bus is started by this firmware. */
#define BRIDGE_MAX17043_SDA_GPIO 8
#define BRIDGE_MAX17043_SCL_GPIO 9
#define BRIDGE_MAX17043_ALERT_GPIO 7

/* N16R8: exclude flash/PSRAM, native USB, strapping pins and onboard LED. */
#define BRIDGE_PERIPHERAL_PIN_OK(p) \
    (((p) >= 1 && (p) <= 18 && (p) != 3) || (p) == 21 || \
     ((p) >= 38 && (p) <= 42) || (p) == 47)
#define BRIDGE_CHECK_PIN(p) _Static_assert(BRIDGE_PERIPHERAL_PIN_OK(p), "Unsafe bridge peripheral GPIO: " #p)
#define BRIDGE_PIN_BIT(p) (UINT64_C(1) << (p))

BRIDGE_CHECK_PIN(CONFIG_BRIDGE_RFID_SS_GPIO);
BRIDGE_CHECK_PIN(CONFIG_BRIDGE_RFID_SCK_GPIO);
BRIDGE_CHECK_PIN(CONFIG_BRIDGE_RFID_MOSI_GPIO);
BRIDGE_CHECK_PIN(CONFIG_BRIDGE_RFID_MISO_GPIO);
BRIDGE_CHECK_PIN(CONFIG_BRIDGE_RFID_RST_GPIO);

/* Count distinct pins including the future MAX17043, even before it is fitted. */
_Static_assert(__builtin_popcountll(
    BRIDGE_PIN_BIT(CONFIG_BRIDGE_RFID_SS_GPIO) |
    BRIDGE_PIN_BIT(CONFIG_BRIDGE_RFID_SCK_GPIO) |
    BRIDGE_PIN_BIT(CONFIG_BRIDGE_RFID_MOSI_GPIO) |
    BRIDGE_PIN_BIT(CONFIG_BRIDGE_RFID_MISO_GPIO) |
    BRIDGE_PIN_BIT(CONFIG_BRIDGE_RFID_RST_GPIO) |
    BRIDGE_PIN_BIT(BRIDGE_MAX17043_SDA_GPIO) |
    BRIDGE_PIN_BIT(BRIDGE_MAX17043_SCL_GPIO) |
    BRIDGE_PIN_BIT(BRIDGE_MAX17043_ALERT_GPIO) |
    BRIDGE_PIN_BIT(POGO_PIN_TX) | BRIDGE_PIN_BIT(POGO_PIN_RX) |
    BRIDGE_PIN_BIT(POGO_PIN_DET) |
    BRIDGE_PIN_BIT(BRIDGE_UART_TX_GPIO) | BRIDGE_PIN_BIT(BRIDGE_UART_RX_GPIO)
) == 13, "RC522 / MAX17043 / LinkIt / UART GPIO conflict");

#if !CONFIG_BOOT_ROM_LOG_ALWAYS_ON
#error "Bridge builds must retain the default ROM log scheme (no log eFuse writes)."
#endif
