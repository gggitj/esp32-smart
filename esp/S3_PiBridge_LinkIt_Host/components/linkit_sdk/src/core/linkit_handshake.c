/**
 * @file linkit_handshake.c
 * @brief LinkIt 低速握手流程实现
 *
 * @details
 * 该文件实现主机与模块之间的 UART 低速握手协议：
 * 1. 主机发送 "WHO?" 查询
 * 2. 模块返回包含 magic、MAC 地址的握手包
 * 3. 主机解析并缓存模块身份信息（MAC）
 * 4. 主机发送带状态的 ACK 确认消息
 *
 * 这一步确保有线链路下能够完成身份确认与模式切换。
 */

#include "linkit_core_internal.h"
#include "linkit_pins.h"
#include "linkit_transport.h"

#include "driver/uart.h"
#include "esp_mac.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "LINKIT_HS_HOST";

/*
 * 在 UART 字节流中寻找 LNK1，再收齐剩余握手字段。
 * 这样即使切速或重启时留下了半包/杂字节，下一轮握手也能重新对齐。
 */
static int read_aligned_handshake(linkit_handshake_t *info, TickType_t ticks_to_wait)
{
    const uint8_t *magic = (const uint8_t *)LINKIT_MAGIC;
    TickType_t start = xTaskGetTickCount();
    size_t magic_got = 0;

    while ((xTaskGetTickCount() - start) < ticks_to_wait) {
        TickType_t elapsed = xTaskGetTickCount() - start;
        TickType_t remaining = ticks_to_wait - elapsed;
        uint8_t ch = 0;

        if (linkit_uart_read_bytes(&ch, 1, remaining) != 1) {
            break;
        }

        if (ch == magic[magic_got]) {
            magic_got++;
            if (magic_got == LINKIT_MAGIC_LEN) {
                memcpy(info->magic, LINKIT_MAGIC, LINKIT_MAGIC_LEN);
                elapsed = xTaskGetTickCount() - start;
                remaining = (elapsed < ticks_to_wait) ? (ticks_to_wait - elapsed) : 0;
                int rest = linkit_uart_read_exact(
                    ((uint8_t *)info) + LINKIT_MAGIC_LEN,
                    sizeof(*info) - LINKIT_MAGIC_LEN,
                    remaining);
                return LINKIT_MAGIC_LEN + rest;
            }
        } else {
            magic_got = (ch == magic[0]) ? 1U : 0U;
        }
    }

    return (int)magic_got;
}

/* 执行一次低速握手事务：WHO? -> 握手包 -> ACK，成功返回 true。 */
bool linkit_try_handshake_once(uint8_t out_peer_mac[6], char out_module_type[LINKIT_MODULE_TYPE_LEN])
{
    linkit_handshake_t info = {0};

    linkit_uart_flush_input();
    int query_written = linkit_uart_write_bytes(LINKIT_HANDSHAKE_QUERY, LINKIT_HANDSHAKE_QUERY_LEN);
    if (query_written != LINKIT_HANDSHAKE_QUERY_LEN) {
        ESP_LOGE(TAG, "WHO? send failed: written=%d expect=%u",
                 query_written, (unsigned)LINKIT_HANDSHAKE_QUERY_LEN);
    }

    int got = read_aligned_handshake(&info, pdMS_TO_TICKS(LINKIT_HOST_HANDSHAKE_READ_MS));
    if (got != (int)sizeof(info)) {
        ESP_LOGW(TAG, "串口握手超时，未收到从机信息包 (got=%d expect=%u)", got, (unsigned)sizeof(info));
        return false;
    }
    if (memcmp(info.magic, LINKIT_MAGIC, LINKIT_MAGIC_LEN) != 0) {
        ESP_LOGW(TAG, "收到握手包但 magic 无效，重新握手");
        /* magic 失败常见根因：S3 仍在 2 Mbps 发业务流，
         * C6 在 115200 把高速字节当握手包读入。立即 flush 一下
         * 减少冷却期内继续累积的半包字节，避免下一轮再误判。 */
        linkit_uart_flush_input();
        return false;
    }

    memcpy(out_peer_mac, info.mac_addr, 6);
    if (out_module_type != NULL) {
        memcpy(out_module_type, info.module_type, LINKIT_MODULE_TYPE_LEN);
        out_module_type[LINKIT_MODULE_TYPE_LEN - 1] = '\0';
    }

    ESP_LOGI(TAG, "收到从机握手信息: module=%s", info.module_type);

    linkit_host_ack_t ack = {0};
    memcpy(ack.status, LINKIT_HANDSHAKE_ACK, LINKIT_HANDSHAKE_ACK_LEN);
    esp_read_mac(ack.host_mac, ESP_MAC_WIFI_STA);

    linkit_uart_write_bytes(&ack, sizeof(ack));
    uart_wait_tx_done(POGO_UART_NUM, pdMS_TO_TICKS(120));
    ESP_LOGI(TAG, "ACK 已发送，主机 MAC=%02X:%02X:%02X:%02X:%02X:%02X",
             ack.host_mac[0], ack.host_mac[1], ack.host_mac[2],
             ack.host_mac[3], ack.host_mac[4], ack.host_mac[5]);
    return true;
}
