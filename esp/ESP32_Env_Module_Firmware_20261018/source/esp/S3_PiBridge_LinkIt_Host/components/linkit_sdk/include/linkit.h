/**
 * @file linkit.h
 * @brief LinkIt SDK 应用层唯一推荐入口
 *
 * @details
 * 应用工程优先只 include 本文件。它聚合 LinkIt 的公共配置、状态、帧协议
 * 和核心调用函数；具体握手、状态机、UART/ESP-NOW/SPI 驱动细节由 SDK 内部
 * 模块实现，应用层不需要直接调用内部函数。
 *
 * v2.2 的公开 API 设计目标：
 * - 像 ESP-IDF driver 一样用 install / uninstall 管理生命周期
 * - 用 esp_err_t 表达失败原因，便于 ESP_ERROR_CHECK()
 * - 用 write/read bytes 提供最简单的数据收发体感
 * - 保留 legacy API，方便旧 demo 逐步迁移
 *
 * @note Legacy 区域保留了 int / bool / void 等历史返回值，不要直接套
 * ESP_ERROR_CHECK()。新工程只对返回 esp_err_t 的新 API 使用 ESP_ERROR_CHECK()。
 */
#pragma once

#include "linkit_common.h"
#include "linkit_config.h"
#include "linkit_det.h"
#include "linkit_err.h"
#include "linkit_frame.h"
#include "linkit_handshake.h"
#include "linkit_types.h"

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LinkIt 当前安装角色。
 *
 * @note v2.2 暂时仍保留 SLAVE 命名以兼容旧代码；新文档和新 API 中统一称为 Device。
 */
typedef enum {
    LINKIT_ROLE_HOST = 0,
    LINKIT_ROLE_SLAVE,
    LINKIT_ROLE_DEVICE = LINKIT_ROLE_SLAVE,
} linkit_role_t;

/**
 * @brief LinkIt 有线主链路模式。
 *
 * @note SPI 当前仍是实验/预留路径，默认稳定路径为 UART。
 */
typedef enum {
    LINKIT_MODE_AUTO = 0,
    LINKIT_MODE_UART,
    LINKIT_MODE_SPI,
} linkit_mode_t;

/**
 * @brief LinkIt 控制策略。
 *
 * @details 目前主要用于兼容旧模式配置，后续会收敛到 install config。
 */
typedef enum {
    LINKIT_CTRL_SOFT = 0,
    LINKIT_CTRL_HARD,
} linkit_ctrl_t;

/**
 * @brief 旧版模式配置结构。
 *
 * @deprecated 新工程优先使用 linkit_host_config_t / linkit_device_config_t。
 */
typedef struct {
    linkit_mode_t mode;
    linkit_ctrl_t ctrl;
    /**
     * UART 模式下表示握手后的高速波特率目标；低速握手底座仍由协议配置决定。
     * 传 0 表示使用 SDK 默认值。
     */
    unsigned int speed;
} linkit_mode_cfg_t;

/* -------------------------------------------------------------------------- */
/* 生命周期 API                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief 安装 Host 角色驱动。
 *
 * @details Host 通常运行在 C6 主控侧，负责 DET 接入探测、低速握手发起、
 * 高速链路切换和业务数据接收。
 */
esp_err_t linkit_host_install(const linkit_host_config_t *config);

/**
 * @brief 安装 Device 角色驱动。
 *
 * @details Device 通常运行在 S3 模块侧，负责响应 WHO?、上报 LNK1、
 * 进入高速链路并发送业务数据。
 */
esp_err_t linkit_device_install(const linkit_device_config_t *config);

/**
 * @brief 卸载当前 LinkIt 驱动实例。
 *
 * @details 无论当前安装的是 Host 还是 Device，都使用这一个卸载入口。
 */
esp_err_t linkit_uninstall(void);

/**
 * @brief 推进 LinkIt 状态机。
 *
 * @details v2.2 采用显式轮询模型。应用可在主循环或专用 task 中周期性调用。
 */
esp_err_t linkit_poll(TickType_t ticks_to_wait);

/* -------------------------------------------------------------------------- */
/* 数据收发 API                                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief 发送一段 payload，SDK 内部负责封装通用帧。
 *
 * @param data 待发送数据。
 * @param len 数据长度，不能超过 LINKIT_FRAME_MAX_PAYLOAD。
 * @param ticks_to_wait 发送等待时间。
 */
esp_err_t linkit_write_bytes(const void *data, size_t len, TickType_t ticks_to_wait);

/**
 * @brief 读取一段 payload，SDK 内部负责解析通用帧。
 *
 * @param out 输出缓冲区。
 * @param inout_len 输入为缓冲区大小，输出为实际复制长度。
 * @param ticks_to_wait 读取等待时间。
 *
 * @return ESP_OK 读取成功；ticks_to_wait 为 0 且当前无完整业务帧时返回
 * ESP_ERR_LINKIT_NO_DATA；ticks_to_wait 大于 0 且等待超时时返回 ESP_ERR_TIMEOUT。
 */
esp_err_t linkit_read_bytes(void *out, size_t *inout_len, TickType_t ticks_to_wait);

/**
 * @brief 发送一帧 v2.1 通用数据帧。
 *
 * @details 适合需要指定 module_id、cmd_id、msg_type 的复杂业务。
 */
esp_err_t linkit_write_frame(const linkit_frame_t *frame, TickType_t ticks_to_wait);

/**
 * @brief 读取一帧 v2.1 通用数据帧。
 *
 * @return ESP_OK 读取成功；ticks_to_wait 为 0 且当前无完整帧时返回
 * ESP_ERR_LINKIT_NO_DATA；ticks_to_wait 大于 0 且等待超时时返回 ESP_ERR_TIMEOUT。
 */
esp_err_t linkit_read_frame(linkit_rx_frame_t *out, TickType_t ticks_to_wait);

/* -------------------------------------------------------------------------- */
/* 状态与控制 API                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief 查询当前链路状态快照。
 */
esp_err_t linkit_get_link_status(linkit_link_status_t *out);

/**
 * @brief 强制回到握手底座。
 *
 * @details 该接口用于人工断联、UI 重置或异常恢复场景。调用后 SDK 会回到
 * UART 低速握手入口，等待下一轮接入探测。
 */
esp_err_t linkit_force_handshake_reset(void);

/* -------------------------------------------------------------------------- */
/* Legacy 兼容 API                                                            */
/* -------------------------------------------------------------------------- */

/**
 * @deprecated 旧 Host 初始化入口；新工程请使用 linkit_host_install()。
 */
int linkit_init(void);

/**
 * @deprecated 旧卸载入口；新工程请使用 linkit_uninstall()。
 */
int linkit_deinit(void);

/**
 * @deprecated 运行时切角色接口；新工程应在 install 时确定 Host/Device。
 */
int linkit_set_role(linkit_role_t role);

/**
 * @brief 查询当前安装的角色。
 *
 * @details 供诊断代码按 Host/Device 采用不同判据，例如 linkit_probe_pogo_pins()
 * 对 DET 线的预期在两侧是相反的。
 */
linkit_role_t linkit_get_role(void);

/**
 * @deprecated 旧模式设置接口；新工程应使用 install config。
 */
int linkit_set_mode(linkit_mode_cfg_t cfg);

/**
 * @deprecated 查询 legacy 模式视图；新工程请使用 linkit_get_link_status()。
 */
linkit_mode_t linkit_get_mode(void);

/**
 * @deprecated 旧轮询入口；新工程请使用 linkit_poll()。
 */
void linkit_process(void);

/**
 * @deprecated 旧 Host 初始化别名；新工程请使用 linkit_host_install()。
 */
void linkit_host_init(void);

/**
 * @deprecated 查询 legacy 链路状态枚举；新工程请使用 linkit_get_link_status()。
 */
linkit_state_t linkit_get_state(void);

/**
 * @deprecated 读取 legacy linkit_data_t；新工程请使用 linkit_read_bytes()
 * 或 linkit_read_frame()。
 */
bool linkit_get_data(linkit_data_t *out_data);

/**
 * @deprecated 读取 legacy linkit_data_t 的 esp_err_t 包装。
 */
esp_err_t linkit_read_legacy_data(linkit_data_t *out, TickType_t ticks_to_wait);

/**
 * @deprecated 写入 legacy linkit_data_t 的 esp_err_t 包装。
 */
esp_err_t linkit_write_legacy_data(const linkit_data_t *data, TickType_t ticks_to_wait);

/**
 * @brief 获取低速握手阶段识别到的对端模块类型。
 */
bool linkit_get_peer_module_type(char *out_module_type, size_t out_len);

/**
 * @deprecated 旧 Device 初始化入口；新工程请使用 linkit_device_install()。
 */
void linkit_slave_init(const char *module_type);

/**
 * @deprecated 旧 Device 数据更新入口；新工程请使用 linkit_write_bytes()
 * 或 linkit_write_frame()。
 */
void linkit_slave_set_data(const linkit_data_t *data);

/**
 * @deprecated 旧 Device 有线状态查询；新工程请使用 linkit_get_link_status()。
 */
bool linkit_slave_is_wired(void);

/**
 * @deprecated 旧 Device Host MAC 状态查询；新工程请使用 linkit_get_link_status()。
 */
bool linkit_slave_has_host_mac(void);

/**
 * @deprecated 旧人工回滚入口；新工程请使用 linkit_force_handshake_reset()。
 */
void linkit_force_rollback_to_handshake(void);

/* -------------------------------------------------------------------------- */
/* 诊断与实验接口                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief 单根 POGO 线的驱动状态。
 *
 * @details 由内部上拉/下拉两次读取的组合判定，用于在不接示波器的情况下
 * 判断线上挂的是推挽输出还是悬空。
 */
typedef enum {
    LINKIT_PIN_STATE_DRIVEN_HIGH = 0, /**< 上拉下拉都读高：对端推挽驱动高（UART TX idle） */
    LINKIT_PIN_STATE_DRIVEN_LOW,      /**< 上拉下拉都读低：对端推挽驱动低（DET 输出低） */
    LINKIT_PIN_STATE_FLOATING,        /**< 跟随内部拉：无人驱动，或对端只有弱上拉（UART RX） */
    LINKIT_PIN_STATE_CONFLICT,        /**< 读数矛盾：外部强上下拉或探测失败 */
} linkit_pin_state_t;

/**
 * @brief 三根 POGO 线的探测结果快照。
 */
typedef struct {
    linkit_pin_state_t tx;  /**< POGO_PIN_TX 线上观测到的驱动状态 */
    linkit_pin_state_t rx;  /**< POGO_PIN_RX 线上观测到的驱动状态 */
    linkit_pin_state_t det; /**< POGO_PIN_DET 线上观测到的驱动状态 */
} linkit_pogo_probe_t;

/**
 * @brief 把 linkit_pin_state_t 转成日志可读字符串。
 */
const char *linkit_pin_state_name(linkit_pin_state_t state);

/**
 * @brief 三态探测 POGO 三根线，区分「DET 没拉低」和「串口线接反」。
 *
 * @details 对每根线先开内部上拉读一次电平，再开内部下拉读一次；两次都读到
 * 同一电平说明对端在推挽驱动，跟着内部拉走说明没人驱动。函数会把原始结果和
 * 人话结论一起打到日志（TAG=LINKIT_PINPROBE）。
 *
 * 判定依据是 Device 侧的固定行为：TX idle 推挽高、RX 只有内部弱上拉、
 * DET 推挽输出低。因此正确接线时的预期结果是
 * TX=FLOATING、RX=DRIVEN_HIGH、DET=DRIVEN_LOW。
 *
 * @param out 可为 NULL；非 NULL 时输出本次探测快照。
 *
 * @note 探测期间会短暂（约 18ms）把三个脚从外设矩阵上摘下来。若调用时 UART
 * 已打开，函数返回前会自动还原 UART 引脚映射，因此可在链路运行期间重复调用，
 * 代价是可能打断当轮握手，下一轮会自动重试。
 * @note 需要对端模块已上电并完成自身引脚初始化，否则会读到三根线全悬空。
 */
esp_err_t linkit_probe_pogo_pins(linkit_pogo_probe_t *out);

/**
 * @brief 扫描一批候选 GPIO，找出哪个脚上真的有对端 UART 活动。
 *
 * @details 用于「当前 linkit_pins.h 里的 GPIO 选错了」这类问题：把候选脚全部配成
 * 输入+内部上拉，然后在 window_ms 内高速轮询 GPIO 输入寄存器，记录哪些脚出现过电平
 * 跳变。没接东西的脚会被内部上拉稳定拉到 1 不动；接到对端推挽 UART TX 的脚会被
 * 拽出跳变。因此结果掩码里置位的脚就是候选的 RX 接入点。
 *
 * 使用前提是对端正在周期性发送（Host 每 500ms 发一次 WHO?），所以 window_ms
 * 建议不小于 1200，保证窗口内至少覆盖一个完整发送周期。
 *
 * 日志会逐脚给出静止电平和边沿数，用来区分真流量和串扰毛刺：UART RX 线静止必须
 * 是高电平；115200 下一个 "WHO?"(4 字节) 约 20 个边沿，1.2s 窗口内真信号应有几十个。
 *
 * @param candidate_mask 候选脚位掩码，支持 GPIO0~63（bit N 对应 GPIO N）。
 *        调用者必须自行排除 SPI flash / PSRAM / 控制台 UART / 正在使用的外设脚。
 * @param window_ms 采样窗口毫秒数。
 * @param out_active_mask 可为 NULL；非 NULL 时输出发生过跳变的脚位掩码。
 *
 * @note 采样期间是忙等，不让出 CPU，会阻塞本任务和所有更低优先级任务 window_ms 毫秒。
 * 仅作为诊断手段使用，不要放进正常业务路径。
 * @note 扫描会重配候选脚；返回前会按角色还原 DET，并在 UART 原本打开时还原 UART
 * 引脚映射。但候选脚里若包含其它外设正在用的脚，该外设不会被还原。
 */
esp_err_t linkit_scan_gpio_activity(uint64_t candidate_mask,
                                    uint32_t window_ms,
                                    uint64_t *out_active_mask);

/**
 * @brief 手动注册 ESP-NOW peer。
 *
 * @details 该接口只负责把 peer MAC 交给 ESP-NOW 传输层。正常业务优先通过
 * install config 和握手流程自动绑定 peer。
 */
esp_err_t linkit_espnow_add_peer(const uint8_t *mac);

/**
 * @brief SPI 诊断传输接口。
 *
 * @note SPI 仍是实验路径，不属于默认稳定主链路。
 * @note timeout_ms 的单位固定为毫秒；该实验接口暂不使用 TickType_t。
 */
int linkit_spi_transfer(const void *tx,
                        size_t tx_len,
                        void *rx,
                        size_t rx_len,
                        unsigned int timeout_ms);

/**
 * @brief 手动注册 S3 peer 到 ESP-NOW peer 列表。
 *
 * @deprecated 硬件型号不应出现在公开 API；新工程请使用 linkit_espnow_add_peer()。
 */
void register_s3_peer(const uint8_t *mac);

#ifdef __cplusplus
}
#endif
