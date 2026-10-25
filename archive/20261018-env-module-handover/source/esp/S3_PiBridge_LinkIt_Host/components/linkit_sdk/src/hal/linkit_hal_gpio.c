/**
 * @file linkit_hal_gpio.c
 * @brief LinkIt GPIO HAL：POGO 线三态探针
 *
 * @details
 * 后续 DET 去抖、GPIO 矩阵复用、三线 SPI 预留脚切换等与芯片 GPIO
 * 相关的操作，可以逐步从 core/transport 下沉到本文件。
 *
 * 当前已实现 POGO 线三态探针 linkit_probe_pogo_pins()：
 * 对每根线先开内部上拉读一次电平，再开内部下拉读一次，两次结果组合
 * 即可区分「被对端强驱动为高 / 被对端强驱动为低 / 悬空无人驱动」。
 * 用途是在握手失败时，不接示波器也能判断到底是 DET 没拉低、还是线接反了。
 *
 * Host 和 Device 两侧的判据不同，因为对端行为不同：
 * - Host 看到的 DET 应当是 Device 推挽拉低 -> DRIVEN_LOW
 * - Device 看到的 DET 只是 Host 的输入上拉  -> FLOATING
 * 两侧对 UART 两根线的预期反而一致：收线上应当是对端 idle 高的 TX（DRIVEN_HIGH），
 * 发线上是对端只有内部弱上拉的 RX（FLOATING）。
 */

#include "linkit.h"
#include "linkit_pins.h"
#include "linkit_transport.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "soc/gpio_reg.h"

#include <string.h>

static const char *TAG = "LINKIT_PINPROBE";

/* 内部上/下拉约 45kΩ，POGO 走线容性不大，3ms 足够稳定。 */
#define LINKIT_PIN_SETTLE_US 3000U

/* 活动扫描支持的最大脚号+1。ESP32-S3 到 GPIO48，取 64 覆盖当前所有目标。 */
#define LINKIT_SCAN_MAX_PINS 64

/* 对单根线做一次上拉/下拉对比读取。 */
static linkit_pin_state_t probe_one_pin(int gpio)
{
    if (gpio < 0) {
        return LINKIT_PIN_STATE_CONFLICT;
    }

    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << (uint32_t)gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    /* 先脱开外设矩阵：UART 还占着脚时读到的是外设输出，不是线上真实电平。 */
    gpio_reset_pin((gpio_num_t)gpio);

    if (gpio_config(&cfg) != ESP_OK) {
        return LINKIT_PIN_STATE_CONFLICT;
    }
    esp_rom_delay_us(LINKIT_PIN_SETTLE_US);
    int level_pullup = gpio_get_level((gpio_num_t)gpio);

    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
    if (gpio_config(&cfg) != ESP_OK) {
        return LINKIT_PIN_STATE_CONFLICT;
    }
    esp_rom_delay_us(LINKIT_PIN_SETTLE_US);
    int level_pulldown = gpio_get_level((gpio_num_t)gpio);

    /* 上拉读 1 且下拉也读 1 -> 对端在推挽驱动高，内部弱上拉拽不动。 */
    if (level_pullup == 1 && level_pulldown == 1) {
        return LINKIT_PIN_STATE_DRIVEN_HIGH;
    }
    /* 上拉读 0 且下拉也读 0 -> 对端在推挽驱动低。 */
    if (level_pullup == 0 && level_pulldown == 0) {
        return LINKIT_PIN_STATE_DRIVEN_LOW;
    }
    /* 跟着内部拉走 -> 没人驱动，或者对端只是个弱上拉（IDF 给 UART RX 开了内部上拉）。 */
    if (level_pullup == 1 && level_pulldown == 0) {
        return LINKIT_PIN_STATE_FLOATING;
    }
    return LINKIT_PIN_STATE_CONFLICT;
}

/* 探针用 gpio_reset_pin 拆过外设矩阵，并把脚留在「输入+下拉」。
 * 必须按角色还原，否则：
 * - Host 侧 DET 带着下拉会被 gpio_get_level 永远读成 0（误判模块一直插着）
 * - Device 侧 DET 的输出低会消失，Host 再也看不到接入信号 */
static void restore_det_pin(linkit_role_t role)
{
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << (uint32_t)POGO_PIN_DET,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    if (role == LINKIT_ROLE_DEVICE) {
        /* 与各模块 main 里 init_device_det() 一致：推挽输出低，模拟接入下拉。 */
        cfg.mode = GPIO_MODE_OUTPUT;
        cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        (void)gpio_config(&cfg);
        (void)gpio_set_level((gpio_num_t)POGO_PIN_DET, 0);
        return;
    }

    /* 与 linkit_host_init() 一致：输入 + 内部上拉，低电平表示已吸附。 */
    cfg.mode = GPIO_MODE_INPUT;
    cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    (void)gpio_config(&cfg);
}

const char *linkit_pin_state_name(linkit_pin_state_t state)
{
    switch (state) {
    case LINKIT_PIN_STATE_DRIVEN_HIGH: return "DRIVEN_HIGH";
    case LINKIT_PIN_STATE_DRIVEN_LOW:  return "DRIVEN_LOW";
    case LINKIT_PIN_STATE_FLOATING:    return "FLOATING";
    case LINKIT_PIN_STATE_CONFLICT:
    default:                           return "CONFLICT";
    }
}

/* 收线（本端 RX）判据：两侧一致，都应当挂着对端 idle 推挽高的 TX。 */
static bool verdict_rx(linkit_pin_state_t state)
{
    switch (state) {
    case LINKIT_PIN_STATE_DRIVEN_HIGH:
        ESP_LOGI(TAG, "判定: RX(GPIO%d) 正常接到对端 TX（idle 高）", POGO_PIN_RX);
        return true;
    case LINKIT_PIN_STATE_DRIVEN_LOW:
        ESP_LOGE(TAG, "判定: RX(GPIO%d) 被推挽驱动为低 -> 线上挂的是对端 DET，接反了", POGO_PIN_RX);
        return false;
    case LINKIT_PIN_STATE_FLOATING:
        ESP_LOGE(TAG, "判定: RX(GPIO%d) 悬空/弱上拉 -> 没接到对端 TX（线断，或 RX 对 RX 平行接线）",
                 POGO_PIN_RX);
        return false;
    default:
        ESP_LOGW(TAG, "判定: RX(GPIO%d) 读数矛盾，检查外部强上下拉", POGO_PIN_RX);
        return false;
    }
}

/* 发线（本端 TX）判据：两侧一致，对端 RX 只有内部弱上拉，所以期望 FLOATING。 */
static bool verdict_tx(linkit_pin_state_t state)
{
    switch (state) {
    case LINKIT_PIN_STATE_FLOATING:
        ESP_LOGI(TAG, "判定: TX(GPIO%d) 接到对端 RX（弱上拉），符合预期", POGO_PIN_TX);
        return true;
    case LINKIT_PIN_STATE_DRIVEN_LOW:
        ESP_LOGE(TAG, "判定: TX(GPIO%d) 被对端推挽拉死在低 -> 线上挂的是对端 DET，发不出去", POGO_PIN_TX);
        return false;
    case LINKIT_PIN_STATE_DRIVEN_HIGH:
        ESP_LOGE(TAG, "判定: TX(GPIO%d) 被推挽驱动为高 -> 线上挂的是对端 TX（TX 对 TX），接反了",
                 POGO_PIN_TX);
        return false;
    default:
        ESP_LOGW(TAG, "判定: TX(GPIO%d) 读数矛盾，检查外部强上下拉", POGO_PIN_TX);
        return false;
    }
}

/* DET 判据按角色分：Host 期望被 Device 拉低，Device 只应看到 Host 的输入上拉。 */
static bool verdict_det(linkit_pin_state_t state, linkit_role_t role)
{
    if (role == LINKIT_ROLE_DEVICE) {
        switch (state) {
        case LINKIT_PIN_STATE_FLOATING:
            ESP_LOGI(TAG, "判定: DET(GPIO%d) 弱高 -> 对端是 Host 的输入上拉，符合预期", POGO_PIN_DET);
            return true;
        case LINKIT_PIN_STATE_DRIVEN_HIGH:
            ESP_LOGE(TAG, "判定: DET(GPIO%d) 被推挽驱动为高 -> 线上挂的是对端 UART TX，接反了",
                     POGO_PIN_DET);
            return false;
        case LINKIT_PIN_STATE_DRIVEN_LOW:
            ESP_LOGE(TAG, "判定: DET(GPIO%d) 被外部拉死在低 -> 该脚不是 DET，或对地短路", POGO_PIN_DET);
            return false;
        default:
            ESP_LOGW(TAG, "判定: DET(GPIO%d) 读数矛盾", POGO_PIN_DET);
            return false;
        }
    }

    switch (state) {
    case LINKIT_PIN_STATE_DRIVEN_LOW:
        ESP_LOGI(TAG, "判定: DET(GPIO%d) 正常被模块拉低", POGO_PIN_DET);
        return true;
    case LINKIT_PIN_STATE_DRIVEN_HIGH:
        ESP_LOGE(TAG, "判定: DET(GPIO%d) 被推挽驱动为高 -> 线上挂的是对端 UART TX，接反了", POGO_PIN_DET);
        return false;
    case LINKIT_PIN_STATE_FLOATING:
        ESP_LOGE(TAG, "判定: DET(GPIO%d) 悬空 -> DET 完全没拉低（线没接，或对端没配成输出低）",
                 POGO_PIN_DET);
        return false;
    default:
        ESP_LOGW(TAG, "判定: DET(GPIO%d) 读数矛盾", POGO_PIN_DET);
        return false;
    }
}

static void log_probe_verdict(const linkit_pogo_probe_t *p, linkit_role_t role)
{
    if (p->tx == LINKIT_PIN_STATE_FLOATING &&
        p->rx == LINKIT_PIN_STATE_FLOATING &&
        p->det == LINKIT_PIN_STATE_FLOATING) {
        if (role == LINKIT_ROLE_DEVICE) {
            ESP_LOGE(TAG, "判定: 三根线上都没有推挽驱动 -> Host 的 TX 没有到达任何一个脚"
                          "（Host 没上电 / Host 在 WIRELESS 态关了 UART / POGO 没吸合 / 地没通）");
        } else {
            ESP_LOGE(TAG, "判定: 三根线全悬空 -> 模块没上电 / POGO 没吸合 / 地线没通");
        }
        return;
    }

    bool ok = true;
    ok &= verdict_det(p->det, role);
    ok &= verdict_rx(p->rx);
    ok &= verdict_tx(p->tx);

    if (ok) {
        ESP_LOGI(TAG, "判定: 三根线接法均符合预期，握手失败请查协议/波特率侧");
    }
}

esp_err_t linkit_probe_pogo_pins(linkit_pogo_probe_t *out)
{
    const linkit_role_t role = linkit_get_role();
    const bool uart_was_online = linkit_uart_is_online();

    linkit_pogo_probe_t probe = {
        .tx = probe_one_pin(POGO_PIN_TX),
        .rx = probe_one_pin(POGO_PIN_RX),
        .det = probe_one_pin(POGO_PIN_DET),
    };

    /* 还原：DET 按角色恢复，TX/RX 若原本由 UART 占用则重新挂回外设矩阵。
     * 这样本函数可以在链路运行期间安全重复调用。 */
    restore_det_pin(role);
    if (uart_was_online) {
        (void)uart_set_pin(POGO_UART_NUM, POGO_PIN_TX, POGO_PIN_RX, -1, -1);
        linkit_uart_flush_input();
    }

    ESP_LOGW(TAG, "POGO 线探测(role=%s): TX(GPIO%d)=%s  RX(GPIO%d)=%s  DET(GPIO%d)=%s",
             (role == LINKIT_ROLE_DEVICE) ? "DEVICE" : "HOST",
             POGO_PIN_TX, linkit_pin_state_name(probe.tx),
             POGO_PIN_RX, linkit_pin_state_name(probe.rx),
             POGO_PIN_DET, linkit_pin_state_name(probe.det));
    log_probe_verdict(&probe, role);

    if (out != NULL) {
        *out = probe;
    }
    return ESP_OK;
}

/* 一次采样取回全部 GPIO 输入电平。ESP32-S3 等 32 脚以上的目标有第二组寄存器。 */
static inline uint64_t sample_gpio_inputs(void)
{
    uint64_t v = (uint64_t)REG_READ(GPIO_IN_REG);
#ifdef GPIO_IN1_REG
    v |= ((uint64_t)REG_READ(GPIO_IN1_REG)) << 32;
#endif
    return v;
}

esp_err_t linkit_scan_gpio_activity(uint64_t candidate_mask,
                                    uint32_t window_ms,
                                    uint64_t *out_active_mask)
{
    if (candidate_mask == 0ULL || window_ms == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    const linkit_role_t role = linkit_get_role();
    const bool uart_was_online = linkit_uart_is_online();

    /* 候选脚统一配成输入+内部上拉：没接东西的脚会被稳定拉到 1，
     * 只有被对端推挽驱动的脚才可能出现跳变，避免悬空拾噪产生假阳性。 */
    uint64_t armed_mask = 0ULL;
    for (int gpio = 0; gpio < LINKIT_SCAN_MAX_PINS; ++gpio) {
        if ((candidate_mask & (1ULL << gpio)) == 0ULL) {
            continue;
        }
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << (uint32_t)gpio,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_reset_pin((gpio_num_t)gpio);
        if (gpio_config(&cfg) == ESP_OK) {
            armed_mask |= (1ULL << gpio);
        }
    }

    if (armed_mask == 0ULL) {
        ESP_LOGE(TAG, "活动扫描: 候选脚一个都没配置成功");
        return ESP_FAIL;
    }

    esp_rom_delay_us(LINKIT_PIN_SETTLE_US);

    /* 忙等采样。快路径只有两次 REG_READ + 一次异或，采样率远高于 115200 的
     * 8.68us 位宽；只有真的检测到跳变时才去拆位记账，不拖慢主循环。 */
    static uint16_t edge_count[LINKIT_SCAN_MAX_PINS];
    memset(edge_count, 0, sizeof(edge_count));

    const uint64_t baseline = sample_gpio_inputs() & armed_mask;
    uint64_t prev = baseline;
    uint64_t changed = 0ULL;
    const int64_t window_us = (int64_t)window_ms * 1000;
    const int64_t start_us = esp_timer_get_time();
    while ((esp_timer_get_time() - start_us) < window_us) {
        const uint64_t cur = sample_gpio_inputs() & armed_mask;
        uint64_t diff = cur ^ prev;
        if (diff != 0ULL) {
            changed |= diff;
            prev = cur;
            while (diff != 0ULL) {
                const int bit = __builtin_ctzll(diff);
                if (edge_count[bit] < UINT16_MAX) {
                    edge_count[bit]++;
                }
                diff &= (diff - 1ULL);
            }
        }
    }

    /* 还原被扫描波及的 LinkIt 自有引脚。候选里若含其它外设的脚，此处不负责恢复。 */
    restore_det_pin(role);
    if (uart_was_online) {
        (void)uart_set_pin(POGO_UART_NUM, POGO_PIN_TX, POGO_PIN_RX, -1, -1);
        linkit_uart_flush_input();
    }

    ESP_LOGW(TAG, "活动扫描: 窗口=%ums 候选=0x%016llX 有跳变=0x%016llX",
             (unsigned)window_ms,
             (unsigned long long)armed_mask,
             (unsigned long long)changed);

    if (changed == 0ULL) {
        ESP_LOGE(TAG, "活动扫描: 所有候选脚都没有跳变 -> 对端 TX 没有接到任何一个候选脚上");
        if (out_active_mask != NULL) {
            *out_active_mask = 0ULL;
        }
        return ESP_OK;
    }

    /* 逐脚给出静止电平和边沿数。判据：
     * - UART RX 线静止必须是高电平，静止低说明线是坏的，收不出帧
     * - 115200 下一个 "WHO?"(4 字节) 约 20 个边沿，Host 每 500ms 发一次，
     *   1.2s 窗口内真信号应当有几十个边沿；个位数基本是串扰毛刺 */
    for (int gpio = 0; gpio < LINKIT_SCAN_MAX_PINS; ++gpio) {
        if ((changed & (1ULL << gpio)) == 0ULL) {
            continue;
        }
        const int idle_high = ((baseline >> gpio) & 1ULL) ? 1 : 0;
        const unsigned edges = edge_count[gpio];

        if (idle_high && edges >= 20U) {
            ESP_LOGE(TAG, "活动扫描: >>> GPIO%d 静止=高 边沿=%u -> 真 UART 流量，"
                          "这就是应该配成 POGO_PIN_RX 的脚 <<<", gpio, edges);
        } else if (!idle_high) {
            ESP_LOGW(TAG, "活动扫描: GPIO%d 静止=低 边沿=%u -> 线静止在低电平，"
                          "UART 收不出帧（外部下拉过强/对端没在驱动/接触不良）", gpio, edges);
        } else {
            ESP_LOGW(TAG, "活动扫描: GPIO%d 静止=高 边沿=%u -> 边沿太少，疑似串扰毛刺而非真流量",
                     gpio, edges);
        }
    }

    if (out_active_mask != NULL) {
        *out_active_mask = changed;
    }
    return ESP_OK;
}
