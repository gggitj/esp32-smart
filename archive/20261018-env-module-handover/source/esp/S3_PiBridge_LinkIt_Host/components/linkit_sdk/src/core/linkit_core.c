/**
 * @file linkit_core.c
 * @brief LINKIT SDK 核心通信模块
 * 
 * @details
 * 本模块实现 LINKIT 设备通信框架的核心功能，支持多种通信模式：
 * - UART 115200 低速握手通道（探测、验证阶段）
 * - UART 高速模式（帧接收、数据流处理）
 * - SPI 3-wire 半双工模式（高速数据传输，握手后启用）
 * - ESP-NOW 无线接力模式（物理断开时自动兜底）
 * 
 * @subsystem
 * - DET 引脚去抖与状态检测（POGO 连接器检测）
 * - SPI 总线管理与信号量保护
 * - UART 波特率切换与数据帧解析
 * - 设备握手流程与模式自动切换
 * 
 * @state_machine
 * DISCONNECTED -> (handshake success) -> WIRED
 *       ↓ (DET removed)                 ↓ (DET removed / timeout)
 *    WIRELESS  <- (DET re-inserted) <-
 * 
 * @author LINKIT Team
 * @version 1.0
 */

#include "linkit.h"
#include "linkit_host.h"
#include "linkit_core_internal.h"
#include "linkit_parser.h"
#include "linkit_pins.h"
#include "linkit_transport.h"
#include "diagnostics.h"

#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "LINKIT_SDK";

int linkit_deinit(void);
void linkit_process(void);
void linkit_force_rollback_to_handshake(void);

static linkit_state_t g_state = LINKIT_STATE_DISCONNECTED;
static linkit_data_t g_last_data = {0};
static bool g_has_new_data = false;
static linkit_role_t g_role = LINKIT_ROLE_HOST;
static linkit_mode_cfg_t g_mode_cfg = { .mode = LINKIT_MODE_AUTO, .ctrl = LINKIT_CTRL_SOFT, .speed = 0 };

static spi_device_handle_t g_spi_dev = NULL;
static bool g_spi_bus_owner = false;
static SemaphoreHandle_t g_spi_bus_mux = NULL;
static uint32_t g_spi_last_poll_ms = 0;
static bool g_handshake_done = false;
static bool g_installed = false;
/* 只有 ownership=OWNED_BY_SDK 时，SDK 才初始化和操作 ESP-NOW。 */
static bool g_espnow_enabled = false;
static linkit_parser_t g_v21_parser;
static bool g_v21_parser_ready = false;
static linkit_rx_frame_t g_v21_rx_frame;
static bool g_v21_has_rx_frame = false;
/* 每次解出合法 v2.1 帧（含 keepalive）自增，供 feed_rx_bytes 判断本次是否有合法帧。 */
static uint32_t g_v21_rx_frame_count = 0;
static uint16_t g_v21_tx_seq = 0;
static uint8_t g_local_module_id = LINKIT_MOD_ENV_LEGACY;
/* 人工断联后启用“再次吸附门控”：未经历 det 拔出->插入，不允许自动握手。 */
static bool g_manual_wait_reattach = false;
/* 再次吸附门控的第一阶段标志：是否已观测到 det 拔出。 */
static bool g_manual_det_removed_seen = false;
/* fallback 握手（DET 未检测到但 Host 主动试探）成功后置位；置位期间跳过 DET 拔出兜底，
 * 避免 DET 线未接时刚建好的 WIRED 被立刻拆掉。WIRED 的拆除改由 wired RX watchdog 负责。 */
static bool g_detless_wired = false;
/* 当前版本在“尝试重回有线”期间保留无线收包，
 * 这样握手偶发超时不会让 UI 中间断流。 */

/* --------------------------------------------------------------------------
 * 专利流程映射说明（五引脚双模冗余通信）
 *
 * 1) 接入探测与去抖动
 *    - 输入信号：POGO_PIN_DET（经 linkit_det_get_stable_inserted 去抖）
 *    - 目的：以“稳定吸附状态”作为链路切换与重新握手依据
 *
 * 2) 双模冗余状态机
 *    - 常规路径：DISCONNECTED -> HANDSHAKING -> WIRED
 *    - 断开兜底：DET_REMOVE 触发 WIRED -> WIRELESS
 *    - 重插恢复：DET_INSERT_STABLE 触发 WIRELESS -> DISCONNECTED(握手入口)
 *
 * 3) 人工断联保护策略（UI_FORCE_ROLLBACK）
 *    - 触发后回滚至 UART 115200 握手底座，并打开 g_manual_wait_reattach
 *    - 门控要求必须经历：
 *        WAIT_REATTACH_BLOCK（仍吸附，禁止握手）
 *        -> DET_REMOVE_ARMED（观测到拔出）
 *        -> REATTACH_OK（再次吸附，解锁握手）
 *    - 目标：防止“人工断联后原地自动重连”，确保再次吸附后才恢复有线
 *
 * 4) 可观测性
 *    - diagnostics_log_link_fsm 输出 [FSM] 日志，可追踪状态迁移原因与路径
 * -------------------------------------------------------------------------- */

/* 握手阶段记录对端 MAC，供 ESPNOW 传输层做过滤与 peer 注册。 */
static uint8_t g_peer_mac[6] = {0};
/* 握手阶段记录模块类型，供上层 UI 选择对应页面。 */
static char g_peer_module_type[LINKIT_MODULE_TYPE_LEN] = {0};


static inline uint32_t now_ms(void) {
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

static const char *module_type_from_id(uint8_t module_id)
{
    switch (module_id) {
    case LINKIT_MOD_SCOPE:      return "SCOPE";
    case LINKIT_MOD_LA:         return "LOGIC";
    case LINKIT_MOD_THERMAL:    return "THERMAL";
    case LINKIT_MOD_PMETER:     return "PMETER";
    case LINKIT_MOD_INSTRUMENT: return "INSTRUMENT";
    default:                    return "GENERIC";
    }
}

static uint8_t module_id_from_type(const char *module_type)
{
    if (module_type == NULL) {
        return LINKIT_MOD_ENV_LEGACY;
    }
    if (strncmp(module_type, "SCOPE", LINKIT_MODULE_TYPE_LEN) == 0) {
        return LINKIT_MOD_SCOPE;
    }
    if (strncmp(module_type, "LOGIC", LINKIT_MODULE_TYPE_LEN) == 0) {
        return LINKIT_MOD_LA;
    }
    if (strncmp(module_type, "THERMAL", LINKIT_MODULE_TYPE_LEN) == 0) {
        return LINKIT_MOD_THERMAL;
    }
    if (strncmp(module_type, "PMETER", LINKIT_MODULE_TYPE_LEN) == 0) {
        return LINKIT_MOD_PMETER;
    }
    if (strncmp(module_type, "INSTRUMENT", LINKIT_MODULE_TYPE_LEN) == 0) {
        return LINKIT_MOD_INSTRUMENT;
    }
    return LINKIT_MOD_ENV_LEGACY;
}

static bool linkit_is_runtime_linked(void)
{
    return linkit_get_state() == LINKIT_STATE_WIRED;
}

static void linkit_v21_on_frame(const linkit_frame_t *frame, void *user)
{
    (void)user;
    if (frame == NULL) {
        return;
    }

    g_v21_rx_frame_count++;

    if (frame->msg_type == LINKIT_MSG_EVT && frame->cmd_id == LINKIT_CMD_KEEPALIVE) {
        /* keepalive 仅用于刷新链路活跃判定（上面的帧计数），SDK 内部消费，
         * 不投递给 linkit_read_frame()，避免污染应用层和 rejected_frames 统计。 */
        return;
    }

    /*
     * parser 回调中的 payload 指针只在回调期间有效，因此必须复制到
     * SDK 自己持有的接收槽中，供应用稍后通过 read_frame/read_bytes 取走。
     */
    g_v21_rx_frame.frame = *frame;
    if (frame->payload != NULL && frame->payload_len > 0U) {
        uint16_t len = frame->payload_len;
        if (len > LINKIT_FRAME_MAX_PAYLOAD) {
            len = LINKIT_FRAME_MAX_PAYLOAD;
        }
        memcpy(g_v21_rx_frame.payload, frame->payload, len);
        g_v21_rx_frame.frame.payload = g_v21_rx_frame.payload;
        g_v21_rx_frame.frame.payload_len = len;
    } else {
        g_v21_rx_frame.frame.payload = NULL;
        g_v21_rx_frame.frame.payload_len = 0;
    }
    g_v21_has_rx_frame = true;
}

static void linkit_v21_parser_ensure_ready(void)
{
    if (g_v21_parser_ready) {
        return;
    }

    /* v2.1 通用帧解析器只负责高速运行态，不参与 LNK1 低速握手。 */
    linkit_parser_init(&g_v21_parser, linkit_v21_on_frame, NULL);
    linkit_parser_set_gap_timeout(&g_v21_parser, LINKIT_FRAME_GAP_TIMEOUT_MS);
    g_v21_parser_ready = true;
}

bool linkit_v21_feed_rx_bytes(const uint8_t *data, size_t len, uint32_t now_ms)
{
    if (data == NULL || len == 0U) {
        return false;
    }

    linkit_v21_parser_ensure_ready();
    uint32_t count_before = g_v21_rx_frame_count;
    for (size_t i = 0; i < len; ++i) {
        linkit_parser_feed_byte(&g_v21_parser, data[i], now_ms);
    }
    /* 本批字节是否解出了至少一个合法帧（含 keepalive），供模块侧刷新看门狗。 */
    return (g_v21_rx_frame_count != count_before);
}

void linkit_v21_rx_tick(uint32_t now_ms)
{
    linkit_v21_parser_ensure_ready();
    linkit_parser_tick(&g_v21_parser, now_ms);
}

static uint32_t linkit_get_uart_high_baud(void)
{
    return (g_mode_cfg.speed > LINKIT_BAUD_LOW) ? g_mode_cfg.speed : LINKIT_BAUD_HIGH;
}

static const char *state_name(linkit_state_t state) {
    switch (state) {
        case LINKIT_STATE_DISCONNECTED:
            return "DISCONNECTED";
        case LINKIT_STATE_HANDSHAKING:
            return "HANDSHAKING";
        case LINKIT_STATE_WIRED:
            return "WIRED";
        case LINKIT_STATE_WIRELESS:
            return "WIRELESS";
        default:
            return "UNKNOWN";
    }
}

static void log_fsm(linkit_state_t from,
                    const char *event,
                    linkit_state_t to,
                    const char *detail) {
    diagnostics_log_link_fsm(TAG, state_name(from), event, state_name(to), detail);
}





static int linkit_spi_start_with_hz(uint32_t hz) {
    if (g_spi_bus_mux == NULL) {
        g_spi_bus_mux = xSemaphoreCreateMutex();
        if (g_spi_bus_mux == NULL) {
            ESP_LOGE(TAG, "create spi mutex failed");
            return -1;
        }
    }

    if (g_spi_dev != NULL) {
        spi_bus_remove_device(g_spi_dev);
        g_spi_dev = NULL;
    }

    spi_bus_config_t buscfg = {
        .mosi_io_num = LINKIT_SPI_MOSI,
        .miso_io_num = LINKIT_SPI_MISO,
        .sclk_io_num = LINKIT_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };

    esp_err_t err = spi_bus_initialize(LINKIT_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (err == ESP_OK) {
        g_spi_bus_owner = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        // bus already initialized by another module, continue in shared mode
        g_spi_bus_owner = false;
        ESP_LOGW(TAG, "SPI bus already initialized; continue in shared mode (check existing bus MISO/3-wire settings)");
    } else {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        return -2;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)hz,
        .mode = 0,
        .spics_io_num = LINKIT_SPI_CS,
        .queue_size = 4,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_3WIRE,
    };

    err = spi_bus_add_device(LINKIT_SPI_HOST, &devcfg, &g_spi_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        if (g_spi_bus_owner) {
            spi_bus_free(LINKIT_SPI_HOST);
            g_spi_bus_owner = false;
        }        return -3;
    }

    return 0;
}

static void ensure_spi_started(uint32_t speed) {
    uint32_t hz = speed ? speed : 30000000U;
    if (g_spi_dev == NULL) {
        linkit_spi_start_with_hz(hz);
    }
}

static void linkit_spi_stop(void) {
    if (g_spi_dev != NULL) {
        spi_bus_remove_device(g_spi_dev);
        g_spi_dev = NULL;
    }

    if (g_spi_bus_owner) {
        spi_bus_free(LINKIT_SPI_HOST);
        g_spi_bus_owner = false;
    }
}

// SPI path keeps the original payload protocol: linkit_data_t frame.
static int linkit_spi_poll_frame(unsigned int timeout_ms) {
    linkit_data_t rx_frame = {0};
    uint8_t tx_dummy[sizeof(linkit_data_t)] = {0};
    tx_dummy[0] = LINKIT_SYNC_BYTE;

    int rc = linkit_spi_transfer(tx_dummy, sizeof(tx_dummy), &rx_frame, sizeof(rx_frame), timeout_ms);
    if (rc != 0) {
        return rc;
    }

    if (!linkit_frame_data_sane(&rx_frame)) {
        return -4;
    }

    g_last_data = rx_frame;
    g_has_new_data = true;
    return 0;
}

/* Host 侧默认入口：先准备 DET 检测和 UART 115200 握手通道。 */
void linkit_host_init(void) {
    gpio_config_t det_cfg = {
        .pin_bit_mask = 1ULL << POGO_PIN_DET,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&det_cfg));

    if (g_spi_bus_mux == NULL) {
        g_spi_bus_mux = xSemaphoreCreateMutex();
    }

    linkit_uart_open(LINKIT_BAUD_LOW);
    if (g_espnow_enabled) {
        /* ESP-NOW 依赖 Wi-Fi 已初始化（nvs/netif/event/wifi start）。
         * Device 侧在 ensure_network_ready 里做，Host 侧这里补上，否则 esp_now_init 会 panic。 */
        esp_err_t err = nvs_flash_init();
        if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_ERROR_CHECK(nvs_flash_erase());
            err = nvs_flash_init();
        }
        ESP_ERROR_CHECK(err);

        err = esp_netif_init();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(err);
        }
        err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(err);
        }
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
        /* ESP-NOW 自己管理 channel，不需要 promiscuous 切换。
         * esp_wifi_set_channel 让 ESP-NOW 在 channel 1 工作。 */
        ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));

        (void)linkit_espnow_init();
        /* 初始阶段还不知道 peer MAC，先保持无线接收关闭。 */
        linkit_espnow_set_rx_enabled(false);
    }
    g_state = LINKIT_STATE_DISCONNECTED;
    g_handshake_done = false;
    g_manual_wait_reattach = false;
    g_manual_det_removed_seen = false;
    memset(g_peer_module_type, 0, sizeof(g_peer_module_type));
}

linkit_state_t linkit_get_state(void) {
    /* 从机状态由 device 状态机维护，Host 状态仍由 core.c 维护。 */
    if (g_role == LINKIT_ROLE_DEVICE) {
        return linkit_device_get_state_internal();
    }
    return g_state;
}

bool linkit_get_data(linkit_data_t *data) {
    if (!data || !g_has_new_data) return false;
    memcpy(data, &g_last_data, sizeof(linkit_data_t));
    g_has_new_data = false;
    return true;
}

bool linkit_get_peer_module_type(char *out_module_type, size_t out_len) {
    if (out_module_type == NULL || out_len == 0U || g_peer_module_type[0] == '\0') {
        return false;
    }

    strncpy(out_module_type, g_peer_module_type, out_len - 1U);
    out_module_type[out_len - 1U] = '\0';
    return true;
}

static linkit_flow_state_t current_flow_state(void)
{
    switch (g_state) {
        case LINKIT_STATE_DISCONNECTED:
            return LINKIT_FLOW_S0_DISCONNECTED;
        case LINKIT_STATE_HANDSHAKING:
            return LINKIT_FLOW_S2_LOW_SPEED_HANDSHAKE;
        case LINKIT_STATE_WIRED:
            return LINKIT_FLOW_S4_DUAL_MODE_RUNTIME;
        case LINKIT_STATE_WIRELESS:
            return LINKIT_FLOW_S5_LINK_RECOVERY;
        default:
            return LINKIT_FLOW_S0_DISCONNECTED;
    }
}

static linkit_espnow_state_t current_espnow_state(void)
{
    if (!g_espnow_enabled) {
        return LINKIT_ESPNOW_STATE_DISABLED;
    }
    if (g_state == LINKIT_STATE_WIRELESS) {
        return LINKIT_ESPNOW_STATE_ACTIVE_FALLBACK;
    }
    if (g_peer_mac[0] != 0 || g_peer_mac[1] != 0 || g_peer_mac[2] != 0 ||
        g_peer_mac[3] != 0 || g_peer_mac[4] != 0 || g_peer_mac[5] != 0) {
        return LINKIT_ESPNOW_STATE_PEERED;
    }
    return LINKIT_ESPNOW_STATE_IDLE;
}

/* v2.2 兼容薄包装：先复用现有 v2.1 Host 初始化路径。 */
esp_err_t linkit_host_install(const linkit_host_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_installed) {
        return ESP_ERR_LINKIT_ALREADY_INSTALLED;
    }
    if (config->det.enable_runtime_reuse || config->enable_spi3w_reserved) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (config->uart.tx_gpio >= 0 || config->uart.rx_gpio >= 0 || config->det.gpio_num >= 0) {
        ESP_LOGW(TAG, "uart/det gpio override in config is not applied yet; pins come from linkit_pins.h (POGO_*). Edit linkit_pins.h to change pins.");
    }

    g_role = LINKIT_ROLE_HOST;
    g_espnow_enabled = (config->espnow.ownership == LINKIT_ESPNOW_OWNED_BY_SDK);
    linkit_host_init();
    g_installed = true;
    return ESP_OK;
}

/* v2.2 兼容薄包装：Device 侧仍走现有从机初始化。 */
esp_err_t linkit_device_install(const linkit_device_config_t *config)
{
    if (config == NULL || config->device_ext_desc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_installed) {
        return ESP_ERR_LINKIT_ALREADY_INSTALLED;
    }
    if (config->det.enable_runtime_reuse) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (config->uart.tx_gpio >= 0 || config->uart.rx_gpio >= 0 || config->det.gpio_num >= 0) {
        ESP_LOGW(TAG, "uart/det gpio override in config is not applied yet; pins come from linkit_pins.h (POGO_*). Edit linkit_pins.h to change pins.");
    }

    g_role = LINKIT_ROLE_DEVICE;
    g_local_module_id = config->device_ext_desc->module_id;
    g_espnow_enabled = (config->espnow.ownership == LINKIT_ESPNOW_OWNED_BY_SDK);
    linkit_device_set_espnow_enabled_internal(g_espnow_enabled);
    linkit_device_set_module_type_internal(module_type_from_id(g_local_module_id));
    linkit_device_init_internal();
    g_installed = true;
    return ESP_OK;
}

esp_err_t linkit_uninstall(void)
{
    if (!g_installed) {
        return ESP_ERR_LINKIT_NOT_INSTALLED;
    }
    return (linkit_deinit() == 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t linkit_poll(TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!g_installed) {
        return ESP_ERR_LINKIT_NOT_INSTALLED;
    }
    linkit_process();
    return ESP_OK;
}

esp_err_t linkit_get_link_status(linkit_link_status_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    out->wired_state = current_flow_state();
    out->wireless_state = current_espnow_state();
    out->consuming_wireless = (g_state == LINKIT_STATE_WIRELESS);
    memcpy(out->peer_mac, g_peer_mac, sizeof(out->peer_mac));
    strncpy(out->module_type, g_peer_module_type, sizeof(out->module_type) - 1U);
    out->module_id = (g_role == LINKIT_ROLE_DEVICE)
                         ? g_local_module_id
                         : module_id_from_type(g_peer_module_type);
    out->active_frame_proto = LINKIT_FRAME_VERSION;
    out->active_mtu = sizeof(linkit_data_t);
    out->enabled_features = LINKIT_FC_LEGACY_DATA;
    out->pin_mux_active = LINKIT_MUX_UART_DEFAULT;
    return ESP_OK;
}

esp_err_t linkit_force_handshake_reset(void)
{
    if (!g_installed) {
        return ESP_ERR_LINKIT_NOT_INSTALLED;
    }
    linkit_force_rollback_to_handshake();
    return ESP_OK;
}

esp_err_t linkit_read_legacy_data(linkit_data_t *out, TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!g_installed) {
        return ESP_ERR_LINKIT_NOT_INSTALLED;
    }
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return linkit_get_data(out) ? ESP_OK : ESP_ERR_LINKIT_NO_DATA;
}

esp_err_t linkit_write_legacy_data(const linkit_data_t *data, TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!g_installed) {
        return ESP_ERR_LINKIT_NOT_INSTALLED;
    }
    if (data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_role != LINKIT_ROLE_DEVICE) {
        return ESP_ERR_INVALID_STATE;
    }
    linkit_device_set_data_internal(data);
    return ESP_OK;
}

esp_err_t linkit_read_frame(linkit_rx_frame_t *out, TickType_t ticks_to_wait)
{
    if (!g_installed) {
        return ESP_ERR_LINKIT_NOT_INSTALLED;
    }
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    linkit_v21_parser_ensure_ready();

    TickType_t start = xTaskGetTickCount();

    do {
        /*
         * read_frame 是应用层的简化接收入口：等待数据期间主动推进握手和
         * 运行态状态机。底层 UART 始终只由 linkit_process() 读取。
         */
        linkit_process();

        if (g_v21_has_rx_frame) {
            *out = g_v21_rx_frame;
            out->frame.payload = out->payload;
            g_v21_has_rx_frame = false;
            return ESP_OK;
        }

        if (ticks_to_wait == 0) {
            break;
        }

        TickType_t elapsed = xTaskGetTickCount() - start;
        if (elapsed >= ticks_to_wait) {
            break;
        }
        vTaskDelay(1);
    } while (true);

    return (ticks_to_wait == 0) ? ESP_ERR_LINKIT_NO_DATA : ESP_ERR_TIMEOUT;
}

esp_err_t linkit_write_frame(const linkit_frame_t *frame, TickType_t ticks_to_wait)
{
    (void)ticks_to_wait;
    if (!g_installed) {
        return ESP_ERR_LINKIT_NOT_INSTALLED;
    }
    if (frame == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t wire[LINKIT_FRAME_MAX_WIRE_SIZE];
    size_t wire_len = linkit_frame_encode(frame, wire, sizeof(wire));
    if (wire_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    /* WIRED 态走 UART；WIRELESS 态（DET 拔出后）走 ESP-NOW 兜底。
     * 用 linkit_get_state() 兼容 device 侧（device 状态在 linkit_role_device.c 维护，不在此 g_state）。
     * WIRELESS 仅在 espnow enabled 时可用；DISCONNECTED/HANDSHAKING 不允许发送。 */
    linkit_state_t st = linkit_get_state();
    if (st == LINKIT_STATE_WIRED) {
        int written = linkit_uart_write_bytes(wire, wire_len);
        return (written == (int)wire_len) ? ESP_OK : ESP_FAIL;
    }
    if (st == LINKIT_STATE_WIRELESS && g_espnow_enabled) {
        int sent = linkit_espnow_send_frame(wire, wire_len);
        return (sent == 0) ? ESP_OK : ESP_FAIL;
    }
    return ESP_ERR_LINKIT_NOT_LINKED;
}

esp_err_t linkit_read_bytes(void *out, size_t *inout_len, TickType_t ticks_to_wait)
{
    if (out == NULL || inout_len == NULL || *inout_len == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    linkit_rx_frame_t rx = {0};
    esp_err_t err = linkit_read_frame(&rx, ticks_to_wait);
    if (err != ESP_OK) {
        return err;
    }

    size_t copy_len = rx.frame.payload_len;
    if (copy_len > *inout_len) {
        copy_len = *inout_len;
    }

    const uint8_t *payload = rx.frame.payload != NULL ? rx.frame.payload : rx.payload;
    memcpy(out, payload, copy_len);
    *inout_len = copy_len;
    return ESP_OK;
}

esp_err_t linkit_write_bytes(const void *data, size_t len, TickType_t ticks_to_wait)
{
    if (data == NULL || len == 0U || len > LINKIT_FRAME_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }

    linkit_frame_t frame = {
        .module_id = LINKIT_MOD_ENV_LEGACY,
        .msg_type = LINKIT_MSG_STREAM,
        .flags = LINKIT_FLAG_NONE,
        .cmd_id = 0,
        .seq = g_v21_tx_seq++,
        .payload = (const uint8_t *)data,
        .payload_len = (uint16_t)len,
    };

    return linkit_write_frame(&frame, ticks_to_wait);
}

void linkit_force_rollback_to_handshake(void) {
    linkit_state_t old_state = g_state;

    /* 人工断联时，先统一回退到 UART 低速握手底座并关闭无线接收。 */
    linkit_spi_stop();
    linkit_uart_open(LINKIT_BAUD_LOW);
    linkit_espnow_set_rx_enabled(false);
    linkit_frame_parser_reset();

    g_handshake_done = false;
    g_state = LINKIT_STATE_DISCONNECTED;
    g_manual_wait_reattach = true;
    g_manual_det_removed_seen = false;

    log_fsm(old_state,
            "UI_FORCE_ROLLBACK",
            g_state,
            "uart=115200 wait_reattach=1 espnow_rx=0");
}

void linkit_process(void) {
    /* S3 Device/module 侧必须驱动从机状态机，否则 WHO?/ACK 不会被处理。 */
    if (g_role == LINKIT_ROLE_DEVICE) {
        linkit_device_process_internal();
        return;
    }

    static uint32_t last_poll_time = 0;
    static uint32_t last_valid_data_time = 0;
    static uint32_t last_wired_enter_time = 0;
    static uint32_t last_wait_log_ms = 0;
    static uint32_t last_fallback_poll_ms = 0;
    static bool wired_stream_confirmed = false;

    uint32_t now = now_ms();

    /* 所有模式都先走 UART 115200 握手；只有握手完成后才允许进入 SPI beta。 */
    if (g_mode_cfg.mode == LINKIT_MODE_SPI && g_handshake_done) {
        ensure_spi_started(g_mode_cfg.speed);
        linkit_espnow_set_rx_enabled(false);
        g_state = LINKIT_STATE_WIRED;
        if ((uint32_t)(now - g_spi_last_poll_ms) >= 20U) {
            g_spi_last_poll_ms = now;
            (void)linkit_spi_poll_frame(20);
        }
        return;
    }

    bool det_raw_inserted = (gpio_get_level(POGO_PIN_DET) == 0);
    /* core 只取稳定态，不再持有 DET 去抖细节。 */
    bool det_stable_inserted = linkit_det_get_stable_inserted(det_raw_inserted, now);

    /* --------------------------------------------------------------
     * 人工断联后的“再次吸附门控”：
     * 1) 仍吸附：持续阻断握手（WAIT_REATTACH_BLOCK）
     * 2) 先检测到拔出：标记 DET_REMOVE_ARMED
     * 3) 再检测到重新吸附：解除门控（REATTACH_OK）
     * -------------------------------------------------------------- */
    if (g_manual_wait_reattach) {
        if (!det_stable_inserted) {
            if (!g_manual_det_removed_seen) {
                g_manual_det_removed_seen = true;
                diagnostics_log_link_fsm(TAG,
                                         state_name(g_state),
                                         "DET_REMOVE_ARMED",
                                         state_name(g_state),
                                         "manual rollback observed detach");
            }
        } else if (!g_manual_det_removed_seen) {
            if ((uint32_t)(now - last_wait_log_ms) >= 1000U) {
                last_wait_log_ms = now;
                diagnostics_log_link_fsm(TAG,
                                         state_name(g_state),
                                         "WAIT_REATTACH_BLOCK",
                                         state_name(g_state),
                                         "det still attached after manual rollback");
            }
        } else {
            /* 已完成 det 拔出->插入闭环，允许重新进入握手流程。 */
            g_manual_wait_reattach = false;
            g_manual_det_removed_seen = false;
            diagnostics_log_link_fsm(TAG,
                                     state_name(g_state),
                                     "REATTACH_OK",
                                     state_name(g_state),
                                     "detach->attach observed, handshake unlocked");
        }
    }

    if (!g_manual_wait_reattach && !det_stable_inserted && g_state != LINKIT_STATE_WIRELESS &&
        !g_detless_wired) {
        /* 物理 DET 拔出时，立即转无线兜底并恢复 ESP-NOW 接收。
         * g_detless_wired 期间跳过：fallback 建立的 WIRED 不依赖 DET，由 wired RX watchdog 拆。 */
        linkit_state_t old_state = g_state;
        linkit_uart_close();
        linkit_spi_stop();
        g_handshake_done = false;
        g_detless_wired = false;
        linkit_espnow_set_rx_enabled(true);
        g_state = LINKIT_STATE_WIRELESS;
        log_fsm(old_state, "DET_REMOVE", g_state, "fallback to espnow");
        return;
    }

    if (det_stable_inserted && g_state == LINKIT_STATE_WIRELESS) {
        /* 重新吸附后回到 115200 握手入口，但此时不关闭无线兜底接收。
         * 这样如果本轮 UART 握手失败，业务层仍可持续显示最近的无线数据。 */
        linkit_state_t old_state = g_state;
        linkit_spi_stop();
        linkit_uart_open(LINKIT_BAUD_LOW);
        g_handshake_done = false;
        linkit_espnow_set_rx_enabled(true);
        g_state = LINKIT_STATE_DISCONNECTED;
        log_fsm(old_state, "DET_INSERT_STABLE", g_state, "restart uart 115200");
        return;
    }

    if (g_state == LINKIT_STATE_WIRELESS) {
        /* DET 优先 + fallback：DET 未检测到插入时，Host 仍定期主动试探握手。
         * 这样 DET 线未接/接触不良时，只要对端 Device 在线并回 LNK1，也能完成配对。
         * DET 正常时不会走到这里（DET 检测到插入会先走 DISCONNECTED 握手分支）。
         * 探测节奏比正常握手冷却慢一倍，避免无对端时频繁 open/close UART。 */
        if (!g_manual_wait_reattach &&
            (uint32_t)(now - last_fallback_poll_ms) >= (LINKIT_HANDSHAKE_RETRY_COOLDOWN_MS * 2U)) {
            last_fallback_poll_ms = now;
            linkit_uart_open(LINKIT_BAUD_LOW);
            linkit_espnow_set_rx_enabled(true);
            if (linkit_try_handshake_once(g_peer_mac, g_peer_module_type)) {
                g_handshake_done = true;
                g_detless_wired = true;  /* fallback 路径：DET 未参与，跳过 DET 拔出兜底 */
                if (g_espnow_enabled) {
                    (void)linkit_espnow_add_peer(g_peer_mac);
                }
                /* 握手成功：切高速 UART，进入 WIRED（与 DISCONNECTED 分支一致）。 */
                linkit_state_t old_state = g_state;
                vTaskDelay(pdMS_TO_TICKS(LINKIT_HANDSHAKE_SWITCH_DELAY_MS));
                linkit_uart_set_baud(linkit_get_uart_high_baud());
                linkit_uart_flush_input();
                linkit_espnow_set_rx_enabled(true);
                g_state = LINKIT_STATE_WIRED;
                linkit_frame_parser_reset();
                linkit_v21_parser_ensure_ready();
                linkit_parser_reset(&g_v21_parser);
                g_v21_has_rx_frame = false;
                last_valid_data_time = now;
                last_wired_enter_time = now;
                wired_stream_confirmed = false;
                log_fsm(old_state, "FALLBACK_HANDSHAKE_OK", g_state,
                        "det-less probing succeeded, switch to uart high speed");
                ESP_LOGI(TAG, "Handshake OK (fallback) -> wired UART high-speed: %lu",
                         (unsigned long)linkit_get_uart_high_baud());
            } else {
                /* 探测失败，关闭 UART 回 WIRELESS，等下一轮。 */
                linkit_uart_close();
            }
        }

        /* WIRELESS 态消费 ESP-NOW 接收的 v2.1 frame，喂给 v2.1 parser。
         * 这样 DET 拔出后 Device 经 ESP-NOW 发来的 thermal_frame 能被 Host 收到。 */
        if (g_espnow_enabled) {
            uint8_t wire[LINKIT_FRAME_MAX_WIRE_SIZE];
            size_t len = 0U;
            while (linkit_espnow_try_get_frame(wire, sizeof(wire), &len)) {
                (void)linkit_v21_feed_rx_bytes(wire, len, now);
            }
        }
        return;
    }

    switch (g_state) {
        case LINKIT_STATE_DISCONNECTED:
            if (g_manual_wait_reattach) {
                break;
            }
            /* 低速探测阶段：主机定期发送 WHO?，等待模块返回握手包。 */
            if (det_stable_inserted && (now - last_poll_time > LINKIT_HANDSHAKE_RETRY_COOLDOWN_MS)) {
                last_poll_time = now;
                if (linkit_try_handshake_once(g_peer_mac, g_peer_module_type)) {
                    g_handshake_done = true;
                    g_detless_wired = false;  /* DET 驱动的正常握手，恢复 DET 拔出兜底 */
                    if (g_espnow_enabled) {
                        (void)linkit_espnow_add_peer(g_peer_mac);
                    }

                    if (g_mode_cfg.mode == LINKIT_MODE_SPI) {
                        uint32_t hz = g_mode_cfg.speed ? g_mode_cfg.speed : 30000000U;
                        linkit_uart_close();
                        int rc = linkit_spi_start_with_hz(hz);
                        if (rc == 0) {
                            linkit_state_t old_state = g_state;
                            g_state = LINKIT_STATE_WIRED;
                            g_spi_last_poll_ms = 0;
                            log_fsm(old_state, "HANDSHAKE_OK", g_state, "switch to spi");
                            ESP_LOGI(TAG, "Handshake OK -> switch to SPI mode: %lu Hz", (unsigned long)hz);
                        } else {
                            g_handshake_done = false;
                            linkit_uart_open(LINKIT_BAUD_LOW);
                            g_state = LINKIT_STATE_DISCONNECTED;
                            ESP_LOGW(TAG, "Handshake OK but SPI start failed, stay in handshake loop rc=%d", rc);
                        }
                    } else {
                        linkit_state_t old_state = g_state;
                        /* ACK 发出后按专利约定留出切速缓冲，再统一进入 2 Mbps。 */
                        vTaskDelay(pdMS_TO_TICKS(LINKIT_HANDSHAKE_SWITCH_DELAY_MS));
                        linkit_uart_set_baud(linkit_get_uart_high_baud());
                        linkit_uart_flush_input();
                        /* Keep ESPNOW fallback enabled until the first valid
                         * wired payload arrives. This avoids a visible data gap
                         * when power integrity is marginal during the baud-rate
                         * switch window. */
                        linkit_espnow_set_rx_enabled(true);
                        g_state = LINKIT_STATE_WIRED;
                        /* 切入高速 UART 前，同时清空 legacy 与 v2.1 解析状态。 */
                        linkit_frame_parser_reset();
                        linkit_v21_parser_ensure_ready();
                        linkit_parser_reset(&g_v21_parser);
                        g_v21_has_rx_frame = false;
                        last_valid_data_time = now;
                        last_wired_enter_time = now;
                        wired_stream_confirmed = false;
                        log_fsm(old_state, "HANDSHAKE_OK", g_state, "switch to uart high speed");
                        ESP_LOGI(TAG, "Handshake OK -> wired UART high-speed: %lu",
                                 (unsigned long)linkit_get_uart_high_baud());
                    }
                }
            }
            break;

        case LINKIT_STATE_WIRED: {
            /* 有线保活：周期下发 keepalive 帧，模块侧以此做确定性拔出检测兜底。 */
            static uint32_t last_ka_ms;
            if ((uint32_t)(now - last_ka_ms) >= LINKIT_WIRED_KEEPALIVE_PERIOD_MS) {
                last_ka_ms = now;
                linkit_frame_t ka = {
                    .module_id   = LINKIT_MOD_CORE,
                    .msg_type    = LINKIT_MSG_EVT,
                    .flags       = LINKIT_FLAG_NONE,
                    .cmd_id      = LINKIT_CMD_KEEPALIVE,
                    .seq         = 0,
                    .payload     = NULL,
                    .payload_len = 0,
                };
                (void)linkit_write_frame(&ka, 0);
            }
            /* 高速 UART 工作态：持续收帧，超时则自动回退到 115200。 */
            uint8_t chunk[256];
            size_t drained = 0;
            int r;

            linkit_v21_parser_ensure_ready();
            /*
             * 单次 poll 尽量排空 RX 缓冲，避免应用任务的 10 ms 调度周期
             * 将消费速率限制在 6.4 KB/s。设置排空上限，防止持续输入时
             * 长时间占用当前任务。
             */
            while (drained < 8192U &&
                   (r = linkit_uart_read_bytes(chunk, sizeof(chunk), 0)) > 0) {
                drained += (size_t)r;
                /*
                 * 高速 UART 收到任意字节都说明有线链路仍有活动。
                 * 在完整帧解析成功前先刷新看门狗，避免分段到达的帧触发误回退。
                 */
                last_valid_data_time = now;
                linkit_data_t frame;
                if (linkit_frame_parser_feed(chunk, (size_t)r, now, &frame)) {
                    g_last_data = frame;
                    g_has_new_data = true;
                    if (!wired_stream_confirmed) {
                        wired_stream_confirmed = true;
                        linkit_espnow_set_rx_enabled(false);
                    }
                }
                /*
                 * 同一批 UART 字节同时喂给 legacy 和 v2.1 解析器。
                 * linkit_process() 是 Host 运行态唯一的 UART 读取者。
                 */
                for (int i = 0; i < r; ++i) {
                    linkit_parser_feed_byte(&g_v21_parser, chunk[i], now);
                }
            }

            if (drained == 0U) {
                linkit_parser_tick(&g_v21_parser, now);
            }

            if ((now - last_valid_data_time > LINKIT_WIRED_RX_TIMEOUT_MS) &&
                (now - last_wired_enter_time > LINKIT_WIRED_RX_TIMEOUT_MS)) {
                /* 有线高速流中断时，不直接把业务层“掐死”，而是恢复到
                 * 115200 握手入口并重新打开无线接收，让数据面尽快平滑兜住。 */
                linkit_state_t old_state = g_state;
                linkit_uart_set_baud(LINKIT_BAUD_LOW);
                linkit_espnow_set_rx_enabled(true);
                linkit_frame_parser_reset();
                g_detless_wired = false;  /* 退出 WIRED，恢复正常 DET 优先状态机 */
                g_state = LINKIT_STATE_DISCONNECTED;
                last_wired_enter_time = 0;
                wired_stream_confirmed = false;
                log_fsm(old_state, "WIRED_RX_TIMEOUT", g_state, "rollback uart=115200");
                ESP_LOGW(TAG, "Wired stream timeout, rollback 115200");
            }
            break;
        }

        case LINKIT_STATE_HANDSHAKING:
        case LINKIT_STATE_WIRELESS:
        default:
            break;
    }
}

esp_err_t linkit_espnow_add_peer(const uint8_t *mac) {
    if (mac == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!g_espnow_enabled) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    linkit_espnow_set_peer_mac(mac);
    return (linkit_espnow_register_peer(mac) == 0) ? ESP_OK : ESP_FAIL;
}

void register_s3_peer(const uint8_t *mac) {
    (void)linkit_espnow_add_peer(mac);
}

int linkit_init(void) {
    linkit_host_init();
    return 0;
}

int linkit_deinit(void) {
    linkit_uart_close();
    linkit_spi_stop();
    linkit_espnow_deinit();
    g_state = LINKIT_STATE_DISCONNECTED;
    g_has_new_data = false;
    g_handshake_done = false;
    g_installed = false;
    g_espnow_enabled = false;
    memset(g_peer_module_type, 0, sizeof(g_peer_module_type));
    return 0;
}

int linkit_set_role(linkit_role_t role) {
    g_role = role;
    (void)g_role;
    return 0;
}

linkit_role_t linkit_get_role(void) {
    return g_role;
}

linkit_mode_t linkit_get_mode(void) {
    return g_mode_cfg.mode;
}

int linkit_set_mode(linkit_mode_cfg_t cfg) {
    g_mode_cfg = cfg;
    switch (cfg.mode) {
        case LINKIT_MODE_UART: {
            linkit_spi_stop();
            /* UART mode still enters through the 115200 handshake base; cfg.speed
             * is interpreted as the post-handshake high-speed target. */
            linkit_uart_open(LINKIT_BAUD_LOW);
            linkit_espnow_set_rx_enabled(false);
            g_handshake_done = false;
            g_manual_wait_reattach = false;
            g_manual_det_removed_seen = false;
            g_state = LINKIT_STATE_DISCONNECTED;
            return 0;
        }
        case LINKIT_MODE_AUTO:
            // Return to auto flow; UART side remains available for handshake.
            linkit_spi_stop();
            linkit_uart_open(LINKIT_BAUD_LOW);
            linkit_espnow_set_rx_enabled(false);
            g_handshake_done = false;
            g_manual_wait_reattach = false;
            g_manual_det_removed_seen = false;
            g_state = LINKIT_STATE_DISCONNECTED;
            return 0;
        case LINKIT_MODE_SPI: {
            // Always handshake first on UART 115200, then switch to SPI after handshake.
            linkit_spi_stop();
            linkit_uart_open(LINKIT_BAUD_LOW);
            linkit_espnow_set_rx_enabled(false);
            g_handshake_done = false;
            g_manual_wait_reattach = false;
            g_manual_det_removed_seen = false;
            g_state = LINKIT_STATE_DISCONNECTED;
            return 0;
        }
        default:
            return -1;
    }
}

int linkit_spi_transfer(const void *tx, size_t tx_len, void *rx, size_t rx_len, unsigned int timeout_ms) {
    if (g_spi_dev == NULL) {
        return -1;
    }
    if (g_spi_bus_mux == NULL) {
        return -2;
    }

    TickType_t to = pdMS_TO_TICKS(timeout_ms ? timeout_ms : 100U);
    if (xSemaphoreTake(g_spi_bus_mux, to) != pdTRUE) {
        return -3;
    }

    esp_err_t err = ESP_OK;

    // 3-wire half-duplex: write phase and read phase must be separated.
    if (tx != NULL && tx_len > 0U) {
        spi_transaction_t t_tx = {0};
        t_tx.length = (uint32_t)(tx_len * 8U);
        t_tx.tx_buffer = tx;
        t_tx.rx_buffer = NULL;
        t_tx.rxlength = 0;
        err = spi_device_transmit(g_spi_dev, &t_tx);
        if (err != ESP_OK) {
            xSemaphoreGive(g_spi_bus_mux);
            return (int)err;
        }
    }

    if (rx != NULL && rx_len > 0U) {
        spi_transaction_t t_rx = {0};
        // Half-duplex pure read phase: only enable MISO phase.
        t_rx.length = 0;
        t_rx.tx_buffer = NULL;
        t_rx.rx_buffer = rx;
        t_rx.rxlength = (uint32_t)(rx_len * 8U);
        err = spi_device_transmit(g_spi_dev, &t_rx);
    }

    xSemaphoreGive(g_spi_bus_mux);
    return (err == ESP_OK) ? 0 : (int)err;
}
