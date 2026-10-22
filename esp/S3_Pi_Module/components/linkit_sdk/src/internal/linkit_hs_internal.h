/**
 * @file linkit_hs_internal.h
 * @brief LinkIt v2.2 握手状态机内部接口骨架
 *
 * @details
 * 本文件描述下一阶段 S1-S5 专利流程的内部拆分方式：
 * - S1：接入探测与 DET 稳定确认
 * - S2：低速握手，保持 WHO? / LNK1 / OK! 主线
 * - S3：高速切换，包含 180 ms 延时和 0x55 同步填充
 * - S4：双模运行，UART 主链路 + ESP-NOW 热备
 * - S5：链路自愈，区分帧级、链路级和握手级恢复
 *
 * 当前文件只声明内部钩子，不直接暴露给应用层。
 */
#ifndef LINKIT_HS_INTERNAL_H
#define LINKIT_HS_INTERNAL_H

#include "linkit_config.h"
#include "linkit_handshake.h"
#include "linkit_types.h"

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct linkit_hs_ctx_s linkit_hs_ctx_t;

/* Host 侧 S1-S5 专利流程钩子。 */
esp_err_t linkit_hs_host_s1_wait_det_stable(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_host_s2_send_who(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_host_s2_recv_lnk1(linkit_hs_ctx_t *ctx, linkit_lnk1_packet_t *out);
esp_err_t linkit_hs_host_s2_send_ok(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_host_s3_arm_switch(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_host_s3_consume_sync_pad(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_host_s4_recv_ext_desc(linkit_hs_ctx_t *ctx, linkit_ext_desc_t *out);
esp_err_t linkit_hs_host_s4_send_ext_session(linkit_hs_ctx_t *ctx,
                                             const linkit_ext_session_t *session);
esp_err_t linkit_hs_host_enter_s4(linkit_hs_ctx_t *ctx);
void linkit_hs_host_s5_recover(linkit_hs_ctx_t *ctx,
                               linkit_recover_level_t level,
                               esp_err_t reason);

/* Device 侧 S1-S5 专利流程钩子。 */
esp_err_t linkit_hs_dev_s1_wait_query(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_dev_s2_send_lnk1(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_dev_s2_recv_ok(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_dev_s3_arm_switch(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_dev_s3_emit_sync_pad(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_dev_s4_send_ext_desc(linkit_hs_ctx_t *ctx);
esp_err_t linkit_hs_dev_enter_s4(linkit_hs_ctx_t *ctx);
void linkit_hs_dev_s5_recover(linkit_hs_ctx_t *ctx,
                              linkit_recover_level_t level,
                              esp_err_t reason);

/**
 * @brief 推进握手状态机一步。
 * @details 后续实现中由 install 后的轮询任务或 linkit_poll() 调用。
 */
esp_err_t linkit_hs_run_step(linkit_hs_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
