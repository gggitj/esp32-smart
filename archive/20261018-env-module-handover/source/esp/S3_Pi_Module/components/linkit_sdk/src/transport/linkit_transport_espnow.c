/**
 * @file linkit_transport_espnow.c
 * @brief LinkIt ESP-NOW 热备传输层
 *
 * @details
 * ESP-NOW 在 LinkIt 中不是替代 UART 的“可选主链路”，而是专利双模冗余
 * 路径的一部分：Device 侧可双发，Host 侧在有线异常或 DET 拔出后消费
 * 无线数据。本文件只封装 ESP-NOW 初始化、peer 管理、收发队列和发送接口。
 */

#include "linkit_transport.h"

#include "esp_log.h"
#include "esp_now.h"
#include "freertos/queue.h"

#include <string.h>

/* linkit_frame.h 提供 LINKIT_FRAME_HEAD/HEADER_SIZE/CRC_SIZE/MAX_WIRE_SIZE 等 v2.1 帧常量。 */
#include "linkit_frame.h"

static const char *TAG = "LNK_TP_ESPNOW";

/* 接收队列只保留短历史。
 * 当生产速度超过 UI 消费速度时，优先丢弃最旧帧，让界面尽快收敛到最新值。 */

static QueueHandle_t s_rx_queue = NULL;          /* legacy linkit_data_t 队列 */
static QueueHandle_t s_rx_frame_queue = NULL;    /* v2.1 变长 wire bytes 队列 */
static bool s_inited = false;
static bool s_rx_enabled = false;
static bool s_peer_valid = false;
static uint8_t s_peer_mac[6] = {0};

/* v2.1 frame 队列元素：wire bytes + 长度。 */
#define LINKIT_ESPNOW_FRAME_QUEUE_ITEM_SIZE (LINKIT_FRAME_MAX_WIRE_SIZE + sizeof(size_t))
typedef struct {
    uint8_t wire[LINKIT_FRAME_MAX_WIRE_SIZE];
    size_t len;
} linkit_espnow_rx_item_t;

static inline bool mac_equal(const uint8_t *a, const uint8_t *b) {
    return (memcmp(a, b, 6) == 0);
}

static void on_cmd_sent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
    (void)tx_info;
    ESP_LOGD(TAG, "cmd sent: %s", status == ESP_NOW_SEND_SUCCESS ? "OK" : "FAIL");
}

static void on_data_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    /* 传输层未完全就绪时尽早丢弃，避免 ESP-NOW 热备常开时回调负担过重。 */
    if (!s_inited || !s_rx_enabled) {
        return;
    }
    if (!info || !data || len <= 0) {
        return;
    }
    if (!s_peer_valid || !mac_equal(info->src_addr, s_peer_mac)) {
        return;
    }

    /* legacy linkit_data_t（25 字节，header=0xAA）：存旧队列。 */
    if (len == (int)sizeof(linkit_data_t)) {
        linkit_data_t pkt;
        memcpy(&pkt, data, sizeof(pkt));
        if (pkt.header != LINKIT_FRAME_HEADER) {
            return;
        }
        if (xQueueSend(s_rx_queue, &pkt, 0) != pdTRUE) {
            linkit_data_t drop;
            (void)xQueueReceive(s_rx_queue, &drop, 0);
            (void)xQueueSend(s_rx_queue, &pkt, 0);
        }
        return;
    }

    /* v2.1 frame（header=0xAA，长度 11..LINKIT_FRAME_MAX_WIRE_SIZE）：存 frame 队列。 */
    if (s_rx_frame_queue != NULL &&
        len >= (int)(LINKIT_FRAME_HEADER_SIZE + LINKIT_FRAME_CRC_SIZE) &&
        len <= (int)LINKIT_FRAME_MAX_WIRE_SIZE &&
        data[0] == LINKIT_FRAME_HEAD) {
        linkit_espnow_rx_item_t item;
        memcpy(item.wire, data, (size_t)len);
        item.len = (size_t)len;
        if (xQueueSend(s_rx_frame_queue, &item, 0) != pdTRUE) {
            linkit_espnow_rx_item_t drop;
            (void)xQueueReceive(s_rx_frame_queue, &drop, 0);
            (void)xQueueSend(s_rx_frame_queue, &item, 0);
        }
    }
}

/* 公开 wrapper：供 wireless_dap 的 ESP-NOW recv 分发器回调（非 DAP 帧转给 LinkIt）。
 * 在 wireless_dap remote_dap_recv_cb 内部对非 CMW 帧调用本函数。 */
void linkit_espnow_deliver_recv(const esp_now_recv_info_t *info, const uint8_t *data, int len)
{
    on_data_recv(info, data, len);
}

/**
 * @brief 初始化 ESP-NOW 传输层
 * @return 0 成功；非 0 失败代码
 * @details 分配接收队列、初始化 ESP-NOW、注册回调函数
 */
int linkit_espnow_init(void) {
    if (s_inited) {
        return 0;
    }

    s_rx_queue = xQueueCreate(10, sizeof(linkit_data_t));
    if (s_rx_queue == NULL) {
        ESP_LOGE(TAG, "queue alloc failed");
        return -1;
    }

    s_rx_frame_queue = xQueueCreate(6, sizeof(linkit_espnow_rx_item_t));
    if (s_rx_frame_queue == NULL) {
        ESP_LOGE(TAG, "frame queue alloc failed");
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        return -1;
    }

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_init failed: %d", (int)err);
        vQueueDelete(s_rx_frame_queue);
        s_rx_frame_queue = NULL;
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        return (int)err;
    }

    err = esp_now_register_recv_cb(on_data_recv);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register recv cb failed: %d", (int)err);
        esp_now_deinit();
        vQueueDelete(s_rx_frame_queue);
        s_rx_frame_queue = NULL;
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        return (int)err;
    }

    err = esp_now_register_send_cb(on_cmd_sent);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register send cb failed: %d", (int)err);
        esp_now_unregister_recv_cb();
        esp_now_deinit();
        vQueueDelete(s_rx_frame_queue);
        s_rx_frame_queue = NULL;
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
        return (int)err;
    }

    s_rx_enabled = false;
    s_peer_valid = false;
    s_inited = true;
    return 0;
}

/**
 * @brief 去初始化 ESP-NOW 传输层并释放资源
 * @details 注销回调、去初始化 ESP-NOW、删除接收队列
 */
void linkit_espnow_deinit(void) {
    if (!s_inited) {
        return;
    }

    esp_now_unregister_recv_cb();
    esp_now_unregister_send_cb();
    (void)esp_now_deinit();

    if (s_rx_queue != NULL) {
        vQueueDelete(s_rx_queue);
        s_rx_queue = NULL;
    }

    if (s_rx_frame_queue != NULL) {
        vQueueDelete(s_rx_frame_queue);
        s_rx_frame_queue = NULL;
    }

    s_rx_enabled = false;
    s_peer_valid = false;
    memset(s_peer_mac, 0, sizeof(s_peer_mac));
    s_inited = false;
}

/**
 * @brief 设置对等端 MAC 地址
 * @param mac 对等端 MAC 地址（6 字节），NULL 则清除
 * @details 设置后用于过滤接收数据
 */
void linkit_espnow_set_peer_mac(const uint8_t mac[6]) {
    if (mac == NULL) {
        s_peer_valid = false;
        memset(s_peer_mac, 0, sizeof(s_peer_mac));
        return;
    }
    memcpy(s_peer_mac, mac, 6);
    s_peer_valid = true;
}

/**
 * @brief 注册 ESP-NOW 对等端
 * @param mac 对等端 MAC 地址
 * @return 0 成功；非 0 失败代码
 * @details 添加对等端到 ESP-NOW，并设置内部 MAC
 */
int linkit_espnow_register_peer(const uint8_t mac[6]) {
    if (!s_inited || mac == NULL) {
        return -1;
    }

    if (esp_now_is_peer_exist(mac)) {
        (void)esp_now_del_peer(mac);
    }

    esp_now_peer_info_t peer = {0};
    peer.channel = 0;   /* 0=用当前 Wi-Fi channel（与 wireless_dap 共存时 channel 6） */
    peer.encrypt = false;
    memcpy(peer.peer_addr, mac, 6);

    esp_err_t err = esp_now_add_peer(&peer);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "add peer failed: %d", (int)err);
        return (int)err;
    }

    linkit_espnow_set_peer_mac(mac);
    return 0;
}

/**
 * @brief 启用或禁用接收功能
 * @param enabled true 启用接收；false 禁用
 * @details 控制是否处理接收到的数据
 */
void linkit_espnow_set_rx_enabled(bool enabled) {
    s_rx_enabled = enabled;
}

/**
 * @brief 尝试从接收队列获取数据（非阻塞）
 * @param out_data 输出数据缓冲区
 * @return true 有数据；false 无数据
 * @details 从队列中取出一个数据包，不阻塞
 */
bool linkit_espnow_try_get_data(linkit_data_t *out_data) {
    if (!s_inited || s_rx_queue == NULL || out_data == NULL) {
        return false;
    }
    return xQueueReceive(s_rx_queue, out_data, 0) == pdTRUE;
}

/**
 * @brief 通过 ESP-NOW 发送一帧 LinkIt 数据。
 * @param data 待发送的数据帧。
 * @return 0 表示已提交给 ESP-NOW；非 0 表示当前未初始化、未登记 peer 或底层发送失败。
 *
 * @details
 * 该接口主要服务从机侧双链路热备：握手阶段拿到 Host MAC 并登记 peer 后，
 * 从机会在有线 UART 可用时继续保留 ESP-NOW 发送能力，便于拔出后快速兜底。
 */
int linkit_espnow_send_data(const linkit_data_t *data) {
    if (!s_inited || !s_peer_valid || data == NULL) {
        return -1;
    }

    esp_err_t err = esp_now_send(s_peer_mac, (const uint8_t *)data, sizeof(*data));
    return (err == ESP_OK) ? 0 : (int)err;
}

/*
 * 发送 v2.1 变长 wire bytes（单包，≤ ESP_NOW_MAX_DATA_LEN_V2=1470）。
 * 用于 WIRELESS 态（DET 拔出后）Device→Host 的 thermal_frame 等大帧。
 */
int linkit_espnow_send_frame(const uint8_t *wire, size_t len) {
    if (!s_inited || !s_peer_valid || wire == NULL || len == 0U) {
        return -1;
    }
    if (len > (size_t)ESP_NOW_MAX_DATA_LEN_V2) {
        ESP_LOGW(TAG, "frame too large for espnow: %u > %u", (unsigned)len, (unsigned)ESP_NOW_MAX_DATA_LEN_V2);
        return -1;
    }
    esp_err_t err = esp_now_send(s_peer_mac, wire, len);
    return (err == ESP_OK) ? 0 : (int)err;
}

/*
 * 从 v2.1 frame 接收队列取一帧 wire bytes（非阻塞）。
 * 返回 true 表示取到一帧，out_len 写入实际长度。
 */
bool linkit_espnow_try_get_frame(uint8_t *out_wire, size_t cap, size_t *out_len) {
    if (!s_inited || s_rx_frame_queue == NULL || out_wire == NULL || cap == 0U) {
        return false;
    }
    linkit_espnow_rx_item_t item;
    if (xQueueReceive(s_rx_frame_queue, &item, 0) != pdTRUE) {
        return false;
    }
    if (item.len > cap) {
        /* 缓冲区不够，丢弃这帧。 */
        if (out_len != NULL) {
            *out_len = item.len;
        }
        return false;
    }
    memcpy(out_wire, item.wire, item.len);
    if (out_len != NULL) {
        *out_len = item.len;
    }
    return true;
}
