/**
 * @file linkit_transport_uart.c
 * @brief LinkIt UART 传输层实现
 *
 * @details
 * 本文件提供基于 ESP-IDF UART 驱动的传输封装，封装功能包括：
 * - 打开/关闭 UART
 * - 波特率设置
 * - 阻塞/非阻塞读写
 * - 缓冲区管理
 * - 在线状态查询
 *
 * 该层对上层逻辑屏蔽了底层驱动细节，确保握手和帧解析逻辑仅关注协议本身。
 */

#include "linkit_transport.h"
#include "linkit_pins.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"

/* 仅维护 UART 传输层自己的在线状态。 */
static bool s_uart_online = false;
static const char *TAG = "LINKIT_UART";

/**
 * @brief 打开 UART 传输通道
 * @param baud 波特率（如 115200、921600）
 * @return 0 成功；非 0 失败代码
 * @details 首次调用会初始化 UART 驱动，后续调用只修改波特率
 */
int linkit_uart_open(uint32_t baud)
{
    if (s_uart_online) {
        return linkit_uart_set_baud(baud);
    }

    uart_config_t uart_config = {
        .baud_rate = (int)baud,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(POGO_UART_NUM, 8192, 2048, 0, NULL, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "driver install failed: port=%d err=%s",
                 POGO_UART_NUM, esp_err_to_name(err));
        return (int)err;
    }

    err = uart_param_config(POGO_UART_NUM, &uart_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "param config failed: port=%d baud=%lu err=%s",
                 POGO_UART_NUM, (unsigned long)baud, esp_err_to_name(err));
        uart_driver_delete(POGO_UART_NUM);
        return (int)err;
    }

    err = uart_set_pin(POGO_UART_NUM, POGO_PIN_TX, POGO_PIN_RX, -1, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set pin failed: port=%d tx=%d rx=%d err=%s",
                 POGO_UART_NUM, POGO_PIN_TX, POGO_PIN_RX, esp_err_to_name(err));
        uart_driver_delete(POGO_UART_NUM);
        return (int)err;
    }

    s_uart_online = true;
    ESP_LOGI(TAG, "open: port=%d tx=GPIO%d rx=GPIO%d baud=%lu",
             POGO_UART_NUM, POGO_PIN_TX, POGO_PIN_RX, (unsigned long)baud);
    return 0;
}

/**
 * @brief 关闭 UART 传输通道并释放资源
 * @details 卸载驱动、复位引脚、恢复为输入状态
 */
void linkit_uart_close(void)
{
    if (!s_uart_online) {
        return;
    }

    uart_flush_input(POGO_UART_NUM);
    uart_disable_rx_intr(POGO_UART_NUM);
    uart_driver_delete(POGO_UART_NUM);

    /* 关闭驱动后把引脚恢复成输入，避免残留外设占用。 */
    gpio_reset_pin(POGO_PIN_RX);
    gpio_set_direction(POGO_PIN_RX, GPIO_MODE_INPUT);
    gpio_set_pull_mode(POGO_PIN_RX, GPIO_PULLDOWN_ONLY);

    gpio_reset_pin(POGO_PIN_TX);
    gpio_set_direction(POGO_PIN_TX, GPIO_MODE_INPUT);

    s_uart_online = false;
}

/**
 * @brief 修改 UART 波特率
 * @param baud 目标波特率
 * @return 0 成功；非 0 失败代码
 * @details 若 UART 未打开，自动调用 open 初始化
 */
int linkit_uart_set_baud(uint32_t baud)
{
    if (!s_uart_online) {
        return linkit_uart_open(baud);
    }
    return (int)uart_set_baudrate(POGO_UART_NUM, (int)baud);
}

/* 用于握手包等固定长度数据的阻塞式读取。 */
/**
 * @brief 阻塞式读取指定长度的数据
 * @param buf 数据缓冲区指针
 * @param len 需要读取的字节数
 * @param ticks_to_wait FreeRTOS 等待超时时间
 * @return 实际读取的字节数
 * @details 适用于握手包等固定长度数据，循环读取直到满足长度要求
 */
int linkit_uart_read_exact(void *buf, size_t len, TickType_t ticks_to_wait)
{
    size_t total_read = 0;
    uint8_t *p = (uint8_t *)buf;

    while (total_read < len) {
        int read = uart_read_bytes(POGO_UART_NUM, p + total_read, len - total_read, ticks_to_wait);
        if (read <= 0) {
            break;
        }
        total_read += (size_t)read;
    }

    return (int)total_read;
}

/**
 * @brief 非阻塞或定时读取数据
 * @param buf 数据缓冲区指针
 * @param len 最多读取的字节数
 * @param ticks_to_wait FreeRTOS 等待超时时间
 * @return 实际读取的字节数
 * @details 一次最多返回 FIFO 中可用的数据，不保证读到指定长度
 */
int linkit_uart_read_bytes(void *buf, size_t len, TickType_t ticks_to_wait)
{
    return uart_read_bytes(POGO_UART_NUM, buf, len, ticks_to_wait);
}

/* 诊断用：返回当前 UART 接收缓冲区中已缓存（未取走）的字节数。 */
int linkit_uart_rx_backlog(void)
{
    size_t n = 0;
    if (uart_get_buffered_data_len(POGO_UART_NUM, &n) != ESP_OK) {
        return -1;
    }
    return (int)n;
}

/**
 * @brief 发送数据到 UART
 * @param buf 待发送的数据缓冲区
 * @param len 待发送的字节数
 * @return 实际发送的字节数
 */
int linkit_uart_write_bytes(const void *buf, size_t len)
{
    return uart_write_bytes(POGO_UART_NUM, buf, len);
}

/**
 * @brief 清空 UART 接收缓冲区
 * @details 丢弃接收 FIFO 中的所有数据
 */
void linkit_uart_flush_input(void)
{
    if (s_uart_online) {
        uart_flush_input(POGO_UART_NUM);
    }
}

/**
 * @brief 检查 UART 是否在线（已打开）
 * @return true UART 已打开；false 已关闭
 */
bool linkit_uart_is_online(void)
{
    return s_uart_online;
}
