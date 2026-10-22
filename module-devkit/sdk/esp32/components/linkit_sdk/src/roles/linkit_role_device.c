/**
 * @file linkit_role_device.c
 * @brief LinkIt Device 瑙掕壊瀹炵幇
 *
 * @details
 * 鏈枃浠舵壙杞芥ā鍧楃鏈€灏忕姸鎬佹満锛?
 * - 鍦?UART 115200 浣庨€熼樁娈电洃鍚?Host 鐨?"WHO?"
 * - 杩斿洖 LNK1 鎻℃墜鍖咃紝鍖呭惈 magic銆乵odule_type 鍜屾湰鏈?MAC
 * - 鎺ユ敹 Host 鐨?"OK!" + host MAC
 * - 鎸変笓鍒╂椂搴忓欢鏃跺悗鍒囨崲鍒伴珮閫?UART锛屽苟鍙戝嚭 0x55 鍚屾濉厖
 * - 杩涘叆杩愯鎬佸悗閫氳繃鏈夌嚎 UART 鍜?ESP-NOW 鐑璺緞鍙戦€佷笟鍔℃暟鎹?
 */

#include "linkit.h"
#include "linkit_pins.h"
#include "linkit_transport.h"
#include "diagnostics.h"
#include "../core/linkit_core_internal.h"

#include "driver/uart.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "LINKIT_SLAVE";

static linkit_state_t s_state = LINKIT_STATE_DISCONNECTED;
static linkit_data_t s_tx_data = {
    .header = LINKIT_FRAME_HEADER,
};
static uint8_t s_host_mac[6] = {0};
static char s_module_type[LINKIT_MODULE_TYPE_LEN] = "GENERIC";
static bool s_has_host_mac = false;
static bool s_wired = false;
static bool s_net_ready = false;
/* UART-only 妯″紡涓嬩负 false锛孌evice 涓嶅垵濮嬪寲 Wi-Fi/ESP-NOW銆?*/
static bool s_espnow_enabled = false;
/* 榛樿鍏抽棴 legacy 鑷姩鍙戦€侊紝閬垮厤姹℃煋浣跨敤 write_bytes() 鐨?v2.1 閾捐矾銆?*/
static bool s_legacy_autosend_enabled = false;
static uint8_t s_who_match = 0;
static uint8_t s_ack_buf[sizeof(linkit_host_ack_t)] = {0};
static size_t s_ack_got = 0;
static uint32_t s_last_send_ms = 0;
static uint32_t s_wired_rx_window_start_ms = 0;
static uint32_t s_wired_rx_byte_count = 0;
static uint32_t s_wired_last_valid_rx_ms = 0;   /* 最后一次成功解出合法帧（含 keepalive）的时刻 */
/* 楂橀€熸湁绾挎€佷笅锛孒ost 鍙兘鍏堝洖鍒?115200 閲嶆柊鎻℃墜銆?
 * Device 鐢ㄦ粦鍔ㄧ獥鍙ｇ粺璁￠珮閫?UART RX 娲诲姩锛岄伩鍏嶅崟瀛楄妭姣涘埡璇Е鍙戝洖閫€銆?
 * C6 鍦?115200 鍙戦€?WHO? 鏃讹紝鍗充娇澶ч儴鍒嗗瓧鑺傚洜娉㈢壒鐜囦笉鍖归厤琚涪寮冿紝
 * 閫氬父浠嶄細鐣欎笅瓒冲 RX 娲诲姩璁╃獥鍙ｉ槇鍊煎懡涓€?*/
/* 璁板綍褰撳墠 ACK 绛夊緟绐楀彛锛屼究浜庣煭鏃堕棿閲嶅彂 LNK1 鍖呭苟鍦ㄨ秴鏃跺悗鍥為€€銆?*/
static uint32_t s_handshake_enter_ms = 0;
static uint32_t s_last_handshake_info_ms = 0;

/* 灏嗙姸鎬佹灇涓捐浆鎹㈡垚鏃ュ織鍙瀛楃涓层€?*/
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

/* Device 鐘舵€佹満鏃ュ織缁熶竴鍑哄彛锛屼繚鎸佷笌 Host [FSM] 鏃ュ織鏍煎紡涓€鑷淬€?*/
static void device_log_fsm(linkit_state_t from, const char *event, linkit_state_t to, const char *detail) {
    diagnostics_log_link_fsm(TAG, state_name(from), event, state_name(to), detail);
}

/* 灏佽鐘舵€佸垏鎹紝閬垮厤鍚勫垎鏀暎钀芥棩蹇楄緭鍑恒€?*/
static void device_set_state(linkit_state_t next, const char *event, const char *detail) {
    linkit_state_t old = s_state;
    s_state = next;
    if (old != next || event != NULL) {
        device_log_fsm(old, event, next, detail);
    }
}

/* 姣忔鍥炲埌浣庨€熸彙鎵嬫€佹椂锛岄噸缃?WHO?/ACK 瑙ｆ瀽涓婁笅鏂囥€?*/
static void device_reset_handshake_parser(void) {
    s_who_match = 0;
    s_ack_got = 0;
    s_handshake_enter_ms = 0;
    s_last_handshake_info_ms = 0;
    memset(s_ack_buf, 0, sizeof(s_ack_buf));
}

/* Device 渚х綉缁滃簳搴э細鎸変笓鍒╁弻妯″啑浣欒姹傚噯澶?ESP-NOW 鐑閾捐矾銆?*/
static void ensure_network_ready(void) {
    if (s_net_ready) {
        return;
    }

    /*
     * UART-only 绀轰緥涓嶉渶瑕佸惎鍔?Wi-Fi銆俥sp_read_mac() 鍙洿鎺ヨ鍙?STA MAC锛?
     * 鍥犳璺宠繃鏃犵嚎鍒濆鍖栦笉浼氬奖鍝嶄綆閫?UART 鎻℃墜韬唤鍖呫€?
     */
    if (!s_espnow_enabled) {
        s_net_ready = true;
        return;
    }

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
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    ESP_ERROR_CHECK(esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));

    ESP_ERROR_CHECK(linkit_espnow_init());
    s_net_ready = true;
}

/* 鍙戦€?Device 鎻℃墜鍖咃細magic + module_type + Device MAC銆?*/
static void send_handshake_info(void) {
    linkit_handshake_t info = {0};
    memcpy(info.magic, LINKIT_MAGIC, LINKIT_MAGIC_LEN);
    strncpy(info.module_type, s_module_type, sizeof(info.module_type) - 1);
    ESP_ERROR_CHECK(esp_read_mac(info.mac_addr, ESP_MAC_WIFI_STA));
    int written = linkit_uart_write_bytes(&info, sizeof(info));
    esp_err_t tx_done = uart_wait_tx_done(POGO_UART_NUM, pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "handshake_info: written=%d/%u tx_done=%d",
             written, (unsigned)sizeof(info), (int)tx_done);
}

/* 閫愬瓧鑺傚尮閰?Host 鍙戞潵鐨?"WHO?" 鎺㈡祴涓层€?*/
static bool poll_who_request(void) {
    static const uint8_t pat[LINKIT_HANDSHAKE_QUERY_LEN] = {
        LINKIT_HANDSHAKE_QUERY[0],
        LINKIT_HANDSHAKE_QUERY[1],
        LINKIT_HANDSHAKE_QUERY[2],
        LINKIT_HANDSHAKE_QUERY[3],
    };
    uint8_t ch = 0;
    int n = linkit_uart_read_bytes(&ch, 1, 0);
    if (n != 1) {
        return false;
    }

    if (ch == pat[s_who_match]) {
        s_who_match++;
        if (s_who_match == sizeof(pat)) {
            s_who_match = 0;
            return true;
        }
    } else {
        s_who_match = (ch == pat[0]) ? 1U : 0U;
    }
    return false;
}

/* 闈為樆濉炴敹闆?ACK 鍖咃紝鐩村埌鑾峰緱瀹屾暣 host MAC銆?
 * ACK 鍙兘琚?UART 鍒嗙墖閫佽揪锛屽洜姝よ繖閲屾寜澧為噺鏂瑰紡鏀堕泦锛岄伩鍏嶉樆濉炰富寰幆銆?*/
static bool poll_ack_packet(uint8_t out_host_mac[6]) {
    int n = linkit_uart_read_bytes(s_ack_buf + s_ack_got, sizeof(s_ack_buf) - s_ack_got, 0);
    if (n <= 0) {
        return false;
    }

    s_ack_got += (size_t)n;
    if (s_ack_got < sizeof(linkit_host_ack_t)) {
        return false;
    }

    linkit_host_ack_t ack = {0};
    memcpy(&ack, s_ack_buf, sizeof(ack));
    s_ack_got = 0;

    if (memcmp(ack.status, LINKIT_HANDSHAKE_ACK, LINKIT_HANDSHAKE_ACK_LEN) != 0) {
        return false;
    }

    memcpy(out_host_mac, ack.host_mac, 6);
    return true;
}

/* 鍥炲埌 UART 115200 鐩戝惉搴曞骇锛屽噯澶囦笅涓€娆℃彙鎵嬨€?*/
static void enter_uart_low_ready(void) {
    linkit_uart_open(LINKIT_BAUD_LOW);
    linkit_uart_set_baud(LINKIT_BAUD_LOW);
    linkit_uart_flush_input();
    device_reset_handshake_parser();
    s_wired_rx_window_start_ms = 0;
    s_wired_rx_byte_count = 0;
    s_wired = false;
}

/* 鎸変笓鍒┾€淒evice 鍙屽彂鏈哄埗鈥濆彂閫佸綋鍓嶄竴甯т笟鍔℃暟鎹€?
 * 涓€鏃﹁幏寰?Host MAC锛屽氨淇濇寔 ESP-NOW 鐑锛涢珮閫?UART 鐪熸灏辩华鍚庡啀杩藉姞鏈夌嚎鍙戦€併€?*/
static void try_send_current_data(void) {
    /* 鍙湁鏃у簲鐢ㄤ富鍔ㄨ缃繃 legacy 鏁版嵁鍚庯紝鎵嶆仮澶嶅師鏈夌殑鍛ㄦ湡鑷姩鍙戦€佽涔夈€?*/
    if (!s_legacy_autosend_enabled) {
        return;
    }

    uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if ((now - s_last_send_ms) < 20U) {
        return;
    }
    s_last_send_ms = now;

    s_tx_data.header = LINKIT_FRAME_HEADER;
    if (s_espnow_enabled && s_has_host_mac) {
        (void)linkit_espnow_send_data(&s_tx_data);
    }
    if (s_wired) {
        (void)linkit_uart_write_bytes(&s_tx_data, sizeof(s_tx_data));
    }
}

/**
 * @brief 鍒濆鍖?Device 鍐呴儴鐘舵€佹満銆?
 *
 * @details 鍒濆鍖栫綉缁滃拰 ESP-NOW锛屾墦寮€ UART 115200锛屽苟娓呯┖ Host MAC 涓庢彙鎵嬭В鏋愮姸鎬併€?
 */
void linkit_device_init_internal(void) {
    ensure_network_ready();
    enter_uart_low_ready();
    s_has_host_mac = false;
    /* 姣忔閲嶆柊鍒濆鍖?Device 鏃舵仮澶嶄负骞插噣鐨?v2.1 榛樿琛屼负銆?*/
    s_legacy_autosend_enabled = false;
    memset(s_host_mac, 0, sizeof(s_host_mac));
    device_set_state(LINKIT_STATE_DISCONNECTED, "SLAVE_INIT", "uart_115200_wait_who");
}

void linkit_device_set_espnow_enabled_internal(bool enabled) {
    s_espnow_enabled = enabled;
    s_net_ready = false;
}

/**
 * @brief 鎺ㄨ繘 Device 鐘舵€佹満銆?
 *
 * @details 鏈€灏忚矾寰勪负 DISCONNECTED -> HANDSHAKING -> WIRELESS -> WIRED銆?
 * DISCONNECTED/WIRELESS 绛夊緟 WHO?锛孒ANDSHAKING 绛夊緟 OK! ACK锛學IRED 杩涘叆 2 Mbps 鏈夌嚎鍙戦€併€?
 */
void linkit_device_process_internal(void) {
    if (!s_net_ready) {
        return;
    }

    try_send_current_data();

    if (s_state == LINKIT_STATE_WIRED) {
        uint8_t chunk[256];
        int received = linkit_uart_read_bytes(chunk, sizeof(chunk), 0);
        uint32_t rx_now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        if (received > 0) {
            bool got_valid_frame = linkit_v21_feed_rx_bytes(chunk, (size_t)received, rx_now_ms);

            if (got_valid_frame) {
                /* 合法帧 = 链路活着。刷新看门狗，并清零突发计数，
                 * 否则正常下行流量会被误判成拔出噪声（实测 46~55 B/210ms，
                 * 离 64 的阈值只差 9 字节）。 */
                s_wired_last_valid_rx_ms = rx_now_ms;
                s_wired_rx_window_start_ms = 0;
                s_wired_rx_byte_count = 0;
            } else {
                /* sliding-window RX burst detection for Host rollback to 115200 */
                if (s_wired_rx_window_start_ms == 0U ||
                    (rx_now_ms - s_wired_rx_window_start_ms) > LINKIT_WIRED_RX_BREAK_WINDOW_MS) {
                    /* window expired — log before resetting */
                    if (s_wired_rx_window_start_ms != 0U && s_wired_rx_byte_count > 0U) {
                        ESP_LOGI(TAG, "wired rx window ended: %u B / %u ms (thresh=%u)",
                                 (unsigned)s_wired_rx_byte_count,
                                 (unsigned)(rx_now_ms - s_wired_rx_window_start_ms),
                                 (unsigned)LINKIT_WIRED_RX_BREAK_BYTE_THRES);
                    }
                    s_wired_rx_window_start_ms = rx_now_ms;
                    s_wired_rx_byte_count = 0;
                }
                s_wired_rx_byte_count += (uint32_t)received;

                if (s_wired_rx_byte_count >= LINKIT_WIRED_RX_BREAK_BYTE_THRES) {
                    ESP_LOGW(TAG, "UART_ACTIVITY_BREAK: burst=%u B / %u ms (thresh=%u) -> rollback to 115200",
                             (unsigned)s_wired_rx_byte_count,
                             (unsigned)(rx_now_ms - s_wired_rx_window_start_ms),
                             (unsigned)LINKIT_WIRED_RX_BREAK_BYTE_THRES);
                    enter_uart_low_ready();
                    device_set_state(LINKIT_STATE_WIRELESS, "UART_ACTIVITY_BREAK", "rollback_uart_115200");
                }
            }
        } else {
            linkit_v21_rx_tick(rx_now_ms);
            if (s_wired_rx_window_start_ms != 0U &&
                (rx_now_ms - s_wired_rx_window_start_ms) > LINKIT_WIRED_RX_BREAK_WINDOW_MS) {
                /* window went silent — log near-miss events */
                if (s_wired_rx_byte_count >= 8U) {
                    ESP_LOGI(TAG, "wired rx window silent: had %u B / %u ms (did not reach thresh=%u)",
                             (unsigned)s_wired_rx_byte_count,
                             (unsigned)(rx_now_ms - s_wired_rx_window_start_ms),
                             (unsigned)LINKIT_WIRED_RX_BREAK_BYTE_THRES);
                }
                s_wired_rx_window_start_ms = 0;
                s_wired_rx_byte_count = 0;
            }
        }

        /* 确定性兜底：心跳消失即判定拔出，不依赖悬空噪声 */
        if (s_state == LINKIT_STATE_WIRED &&
            (uint32_t)(rx_now_ms - s_wired_last_valid_rx_ms) > LINKIT_WIRED_RX_SILENCE_TIMEOUT_MS) {
            ESP_LOGW(TAG, "WIRED_RX_SILENCE: no valid frame for %u ms -> rollback to 115200",
                     (unsigned)(rx_now_ms - s_wired_last_valid_rx_ms));
            enter_uart_low_ready();
            device_set_state(LINKIT_STATE_WIRELESS, "WIRED_RX_SILENCE", "rollback_uart_115200");
        }
        return;
#if 0  /* 鏃ф粦鍔ㄧ獥鍙ｅ洖閫€妫€娴嬶紝宸茬敱 linkit_v21_feed_rx_bytes 鍙栦唬锛屼繚鐣欏鏌?*/
        uint8_t probe[64];
        int n = linkit_uart_read_bytes(probe, sizeof(probe), 0);
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

        /*
         * 鍙傝€冩渶鏃╃ǔ瀹氱増锛圖:\ESP32Vscode\Sine wave test\S3 Sine wave test\main\main.c锛夛細
         * 鏃у疄鐜版槸浠绘剰 1 瀛楄妭 RX 鍗?break銆傝鏂规鍦ㄥ共鍑€纭欢涓婅兘璺戯紝浣嗗绾胯矾姣涘埡鏁忔劅锛?
         * 鐜板伐绋嬪湪绋虫€佸伓鍙戜細瑙﹀彂璇洖閫€銆傝繖閲屾敼鐢ㄦ粦鍔ㄧ獥鍙ｅ瓧鑺傝鏁帮細
         * - 绐楀彛闀垮害 LINKIT_WIRED_RX_BREAK_WINDOW_MS锛堥粯璁?200 ms锛?
         * - 闃堝€?LINKIT_WIRED_RX_BREAK_BYTE_THRES锛堥粯璁?8 瀛楄妭锛?
         * 鍙湁绐楀彛鍐呯疮璁″瓧鑺傛暟杈惧埌闃堝€兼墠璁ゅ畾涓绘満宸茬粡鍥?115200 鍦ㄩ噸鍙?WHO?锛?
         * 姝ゆ椂鍥為€€鍒?115200 骞跺垏鍒?WIRELESS锛堜粛淇濇寔 ESP-NOW 鍏滃簳鍙戦€侊級銆?
         */
        if (n > 0) {
            if (s_wired_rx_window_start_ms == 0U ||
                (now_ms - s_wired_rx_window_start_ms) > LINKIT_WIRED_RX_BREAK_WINDOW_MS) {
                s_wired_rx_window_start_ms = now_ms;
                s_wired_rx_byte_count = 0;
            }
            s_wired_rx_byte_count += (uint32_t)n;

            if (s_wired_rx_byte_count >= LINKIT_WIRED_RX_BREAK_BYTE_THRES) {
                ESP_LOGW(TAG, "wired rx burst %u B / %u ms, rollback to UART 115200",
                         (unsigned)s_wired_rx_byte_count,
                         (unsigned)LINKIT_WIRED_RX_BREAK_WINDOW_MS);
                enter_uart_low_ready();
                device_set_state(LINKIT_STATE_WIRELESS, "UART_ACTIVITY_BREAK", "rollback_uart_115200");
            }
        } else if (s_wired_rx_window_start_ms != 0U &&
                   (now_ms - s_wired_rx_window_start_ms) > LINKIT_WIRED_RX_BREAK_WINDOW_MS) {
            /* 绐楀彛鏈熷唴娌″啀鏉ュ瓧鑺傦紝瑙嗕负瀛ょ珛姣涘埡锛屾暣浣撴竻闆剁瓑涓嬩竴娆°€?*/
            s_wired_rx_window_start_ms = 0;
            s_wired_rx_byte_count = 0;
        }
        return;
#endif
    }

    if (s_state == LINKIT_STATE_DISCONNECTED || s_state == LINKIT_STATE_WIRELESS) {
        if (poll_who_request()) {
            uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            send_handshake_info();
            s_handshake_enter_ms = now_ms;
            s_last_handshake_info_ms = now_ms;
            ESP_LOGI(TAG, "鏀跺埌 WHO?锛屽凡鍙戦€佹彙鎵嬩俊鎭?module=%s)", s_module_type);
            device_set_state(LINKIT_STATE_HANDSHAKING, "WHO_RECEIVED", "handshake_info_sent");
        }
        /* WIRELESS 态消费 ESP-NOW 接收的 v2.1 frame（Host 下行的控制帧，如 Disconnect），
         * 喂给 v2.1 parser，应用层通过 linkit_read_frame 取走处理。 */
        if (s_state == LINKIT_STATE_WIRELESS && s_espnow_enabled) {
            uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
            uint8_t wire[LINKIT_FRAME_MAX_WIRE_SIZE];
            size_t len = 0U;
            while (linkit_espnow_try_get_frame(wire, sizeof(wire), &len)) {
                (void)linkit_v21_feed_rx_bytes(wire, len, now_ms);
            }
        }
        return;
    }

    if (s_state == LINKIT_STATE_HANDSHAKING) {
        uint32_t now_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        uint8_t host_mac[6] = {0};
        if (!poll_ack_packet(host_mac)) {
            /* ACK 绐楀彛鍐呬細閲嶅彂鎻℃墜淇℃伅鍖呫€?
             * 濡傛灉棣栧寘琚?Host 閿欒繃锛屽悗缁噸鍙戜粛鍙畬鎴愬綋鍓嶅惛闄勫懆鏈燂紝
             * 涓嶅繀绛夊緟瀹屾暣鎷旀彃閲嶆潵銆?*/
            if (s_handshake_enter_ms != 0U &&
                (now_ms - s_handshake_enter_ms) >= LINKIT_SLAVE_ACK_TIMEOUT_MS) {
                ESP_LOGW(TAG, "ACK timeout, rollback to UART 115200");
                enter_uart_low_ready();
                device_set_state(LINKIT_STATE_DISCONNECTED, "ACK_TIMEOUT", "rollback_uart_115200");
                return;
            }

            if (s_last_handshake_info_ms == 0U ||
                (now_ms - s_last_handshake_info_ms) >= LINKIT_SLAVE_INFO_RESEND_MS) {
                send_handshake_info();
                s_last_handshake_info_ms = now_ms;
            }
            return;
        }

        memcpy(s_host_mac, host_mac, sizeof(s_host_mac));
        s_has_host_mac = true;
        if (s_espnow_enabled) {
            (void)linkit_espnow_register_peer(s_host_mac);
        }
        ESP_LOGI(TAG, "ACK 鏈夋晥锛孒ost MAC=%02X:%02X:%02X:%02X:%02X:%02X",
                 s_host_mac[0], s_host_mac[1], s_host_mac[2],
                 s_host_mac[3], s_host_mac[4], s_host_mac[5]);
        device_set_state(LINKIT_STATE_WIRELESS, "ACK_OK", "host_mac_registered");

        uart_wait_tx_done(POGO_UART_NUM, pdMS_TO_TICKS(100));
        vTaskDelay(pdMS_TO_TICKS(LINKIT_HANDSHAKE_SWITCH_DELAY_MS));
        linkit_uart_set_baud(LINKIT_BAUD_HIGH);
        linkit_uart_flush_input();

        uint8_t sync[32];
        memset(sync, LINKIT_SYNC_BYTE, sizeof(sync));
        linkit_uart_write_bytes(sync, sizeof(sync));

        s_wired = true;
        s_last_send_ms = 0;
        /* 看门狗起点：刚进 WIRED 就视为链路活着，否则会被 silence 检测误判拔出。 */
        s_wired_last_valid_rx_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        try_send_current_data();
        device_set_state(LINKIT_STATE_WIRED, "UART_HIGH_READY", "switch_to_2mbps");
        ESP_LOGI(TAG, "杩涘叆鏈夌嚎楂橀€熸ā寮?%d", LINKIT_BAUD_HIGH);
    }
}

/* 鍚?core 灞傛毚闇插綋鍓?Device 鐘舵€併€?*/
linkit_state_t linkit_device_get_state_internal(void) {
    return s_state;
}

/* 鏇存柊鎻℃墜鍖呬腑鐨勬ā鍧楃被鍨嬪瓧娈点€?*/
void linkit_device_set_module_type_internal(const char *module_type) {
    if (module_type == NULL || module_type[0] == '\0') {
        return;
    }
    memset(s_module_type, 0, sizeof(s_module_type));
    strncpy(s_module_type, module_type, sizeof(s_module_type) - 1);
}

/* 鏇存柊 Device 褰撳墠寰呭彂閫佺殑鏁版嵁甯х紦瀛樸€?*/
void linkit_device_set_data_internal(const linkit_data_t *data) {
    if (data == NULL) {
        return;
    }
    s_tx_data = *data;
    s_tx_data.header = LINKIT_FRAME_HEADER;
    /* 璋冪敤 legacy 鏁版嵁璁剧疆鎺ュ彛鍗宠〃绀哄簲鐢ㄩ渶瑕佹棫鐗堝懆鏈熻嚜鍔ㄥ彂閫佽兘鍔涖€?*/
    s_legacy_autosend_enabled = true;
}

/* 鏌ヨ Device 褰撳墠鏄惁宸茬粡杩涘叆鏈夌嚎楂橀€熷彂閫佹€併€?*/
bool linkit_device_is_wired_internal(void) {
    return s_wired;
}

/* 鏌ヨ Device 鏄惁宸茬粡鑾峰緱 Host MAC銆?*/
bool linkit_device_has_host_mac_internal(void) {
    return s_has_host_mac;
}

/* 鍏紑 Device 鍏ュ彛锛氳缃ā鍧楄韩浠藉苟杩涘叆 UART 115200 鎻℃墜鐩戝惉銆?*/
void linkit_slave_init(const char *module_type) {
    linkit_set_role(LINKIT_ROLE_DEVICE);
    linkit_device_set_module_type_internal(module_type);
    linkit_device_init_internal();
}

/* 搴旂敤灞傚彧鏇存柊鏈€鏂版牱鏈紝鍙岄摼璺彂閫佺敱 Device 鐘舵€佹満缁熶竴璋冨害銆?*/
void linkit_slave_set_data(const linkit_data_t *data) {
    linkit_device_set_data_internal(data);
}

/* 缁欏簲鐢ㄥ眰鍜屾棩蹇楁煡璇㈠綋鍓嶆槸鍚﹀凡缁忓垏鍒伴珮閫?UART 鏈夌嚎閾捐矾銆?*/
bool linkit_slave_is_wired(void) {
    return linkit_device_is_wired_internal();
}

/* 鎻℃墜 ACK 涓В鏋愬埌 Host MAC 鍚庯紝ESP-NOW 鐑閾捐矾鎵嶅彲鍙戦€併€?*/
bool linkit_slave_has_host_mac(void) {
    return linkit_device_has_host_mac_internal();
}

