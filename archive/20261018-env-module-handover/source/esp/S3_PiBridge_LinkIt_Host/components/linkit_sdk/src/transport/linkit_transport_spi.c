/**
 * @file linkit_transport_spi.c
 * @brief LinkIt SPI 诊断传输层（Beta）
 *
 * @details
 * SPI 在 v2.2 中仅作为预留/实验路径，不参与默认握手主流程。本文件提供
 * 主机侧 SPI 探测、失败原因统计和调试日志，帮助验证三线或多线复用方案。
 *
 * @note 正式主链路仍为 UART 高速通道；SPI 代码必须与 UART/ESP-NOW 路径隔离。
 */

#include "linkit.h"
#include "linkit_transport.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "LINKIT_SPI";

/**
 * @brief SPI 诊断探测：每秒发送一帧哑数据，解析回帧并打印统计。
 *
 * 仅在 SPI 有线模式建立后生效；通过累计计数器区分以下失败类型：
 *  - rc_err   : transfer 返回非零（驱动层错误）
 *  - all-zero : 回帧全 0x00（从机未驱动 MISO/SDIO）
 *  - all-ff   : 回帧全 0xFF（线路浮空 / 上拉）
 *  - bad_hdr  : 头字节不匹配（时序 / 协议不符）
 *
 * 连续 3 次相同失败类型时输出对应的布线或配置诊断提示。
 */
void linkit_spi_diag_probe_once_per_sec(void) {
    static uint32_t last_probe_ms  = 0;
    static uint8_t  s_tx_seq       = 0;   /* 自增序列号，用于区分每帧 */
    static uint32_t s_ok           = 0;   /* 总成功帧数 */
    static uint32_t s_ok_sig       = 0;   /* 特征字节镜像匹配成功 */
    static uint32_t s_ok_proto     = 0;   /* Linkit 协议头 0xAA 匹配成功 */
    static uint32_t s_fail_rc      = 0;   /* transfer 驱动错误 */
    static uint32_t s_fail_zero    = 0;   /* 全零帧（从机未驱动） */
    static uint32_t s_fail_ff      = 0;   /* 全 0xFF 帧（线路浮空） */
    static uint32_t s_fail_bad_hdr = 0;   /* 头字节不符（时序/协议不符） */

    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now - last_probe_ms) < 1000U) {
        return;  /* 未到 1 s 间隔，直接返回 */
    }
    last_probe_ms = now;

    /* 非 SPI 有线状态时跳过探测 */
    if (linkit_get_mode() != LINKIT_MODE_SPI || linkit_get_state() != LINKIT_STATE_WIRED) {
        return;
    }

    /* 构造哑帧：魔数 0x5A / 序列号 / 0xC3 / 0x3C，期望从机回镜像或协议头 0xAA */
    uint8_t tx_dummy[sizeof(linkit_data_t)] = {0};
    linkit_data_t rx = {0};
    tx_dummy[0] = 0x5A;
    tx_dummy[1] = s_tx_seq++;
    tx_dummy[2] = 0xC3;
    tx_dummy[3] = 0x3C;

    int rc = linkit_spi_transfer(tx_dummy, sizeof(tx_dummy), &rx, sizeof(rx), 50);
    const uint8_t *rb = (const uint8_t *)&rx;
    bool sig_ok   = (rb[0] == 0xA5 && rb[2] == 0x3C && rb[3] == 0xC3); /* 从机镜像特征字节 */
    bool proto_ok = (rx.header == 0xAA);                                  /* Linkit 协议帧头 */

    if (rc == 0 && (sig_ok || proto_ok)) {
        /* --- 成功路径 --- */
        s_ok++;
        if (sig_ok) {
            s_ok_sig++;
            ESP_LOGI(TAG, "SPI_TEST OK(sig) rx=%02X %02X %02X %02X tx_seq=%u rx_seq=%u",
                     rb[0], rb[1], rb[2], rb[3], tx_dummy[1], rb[1]);
        } else {
            s_ok_proto++;
            ESP_LOGI(TAG, "SPI_TEST OK(proto) T=%.2f H=%.2f", rx.temperature, rx.humidity);
        }
    } else {
        /* --- 失败路径：逐字节扫描判断是否全零或全 FF --- */
        bool all_zero = true;
        bool all_ff   = true;
        for (size_t i = 0; i < sizeof(linkit_data_t); ++i) {
            if (rb[i] != 0x00) {
                all_zero = false;
            }
            if (rb[i] != 0xFF) {
                all_ff = false;
            }
        }

        if (rc != 0) {
            s_fail_rc++;
            ESP_LOGW(TAG, "SPI_TEST FAIL rc=%d hdr=0x%02X (transfer error)", rc, rx.header);
        } else if (all_zero) {
            s_fail_zero++;
            ESP_LOGW(TAG, "SPI_TEST FAIL rc=0 hdr=0x00 (all-zero frame)");
        } else if (all_ff) {
            s_fail_ff++;
            ESP_LOGW(TAG, "SPI_TEST FAIL rc=0 hdr=0xFF (all-ff frame)");
        } else {
            s_fail_bad_hdr++;
            ESP_LOGW(TAG, "SPI_TEST FAIL rc=0 hdr=0x%02X raw=%02X %02X %02X %02X tx=%02X %02X %02X %02X",
                     rx.header, rb[0], rb[1], rb[2], rb[3],
                     tx_dummy[0], tx_dummy[1], tx_dummy[2], tx_dummy[3]);
        }

        /* 打印累计统计摘要 */
        ESP_LOGW(TAG,
                 "SPI_DIAG sum ok=%lu sig=%lu proto=%lu rc_err=%lu zero=%lu ff=%lu bad_hdr=%lu",
                 (unsigned long)s_ok,
                 (unsigned long)s_ok_sig,
                 (unsigned long)s_ok_proto,
                 (unsigned long)s_fail_rc,
                 (unsigned long)s_fail_zero,
                 (unsigned long)s_fail_ff,
                 (unsigned long)s_fail_bad_hdr);

        /* 连续 3 次相同失败类型时输出布线 / 配置诊断提示 */
        if (s_fail_zero >= 3U && s_ok == 0U) {
            ESP_LOGW(TAG, "SPI_DIAG hint: likely no slave data drive (CS/MISO/SDIO wiring or slave not armed).");
        } else if (s_fail_ff >= 3U && s_ok == 0U) {
            ESP_LOGW(TAG, "SPI_DIAG hint: line floating/pull-up (check SDIO/MISO continuity and GND).");
        } else if (s_fail_bad_hdr >= 3U && s_ok == 0U) {
            ESP_LOGW(TAG, "SPI_DIAG hint: timing/protocol mismatch (mode/phase/frame schedule).");
        }
    }
}
