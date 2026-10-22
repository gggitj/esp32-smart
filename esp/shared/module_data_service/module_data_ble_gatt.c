#include "module_data_ble_gatt.h"
#include "module_data_ble.h"

#include "esp_log.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "PI_MODULE_BLE";
static module_data_ble_rx_cb_t s_receive;
static void *s_context;
static uint16_t s_connection = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_notify_handle;
static uint16_t s_message_id = 1;
static uint8_t s_own_addr_type;
static module_data_ble_reassembler_t s_rx;
static SemaphoreHandle_t s_tx_lock;
typedef struct { uint8_t *payload; size_t length; } ble_rx_message_t;
static QueueHandle_t s_rx_queue;
static TaskHandle_t s_rx_task;
static SemaphoreHandle_t s_notify_done;
static volatile int s_notify_status;
static bool s_started;

static void ble_rx_task(void *arg)
{
    (void)arg;
    ble_rx_message_t message;
    while (xQueueReceive(s_rx_queue, &message, portMAX_DELAY) == pdTRUE) {
        if (s_receive) s_receive(message.payload, message.length, s_context);
        free(message.payload);
    }
}

static void advertise(void);

static void nimble_host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
    // nimble_port_freertos_deinit already deletes this host task.
}

static const ble_uuid128_t s_service_uuid = BLE_UUID128_INIT(0x01, 0x00, 0x10, 0x6a, 0x0e, 0xf7, 0xf4, 0xa9, 0xe0, 0x4d, 0x4c, 0x21, 0x01, 0x00, 0x86, 0x9b);
static const ble_uuid128_t s_write_uuid = BLE_UUID128_INIT(0x02, 0x00, 0x10, 0x6a, 0x0e, 0xf7, 0xf4, 0xa9, 0xe0, 0x4d, 0x4c, 0x21, 0x01, 0x00, 0x86, 0x9b);
static const ble_uuid128_t s_notify_uuid = BLE_UUID128_INIT(0x03, 0x00, 0x10, 0x6a, 0x0e, 0xf7, 0xf4, 0xa9, 0xe0, 0x4d, 0x4c, 0x21, 0x01, 0x00, 0x86, 0x9b);

static int access_cb(uint16_t connection, uint16_t attribute, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)connection; (void)attribute; (void)arg;
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;
    const uint8_t *payload = NULL; size_t length = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) > 20) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    uint8_t fragment[20];
    uint16_t copied = 0;
    const int result = ble_hs_mbuf_to_flat(ctxt->om, fragment, sizeof(fragment), &copied);
    if (result != 0 || copied > sizeof(fragment)) return BLE_ATT_ERR_UNLIKELY;
    if (!module_data_ble_reassembler_push(&s_rx, fragment, (size_t)copied, &payload, &length)) return 0;
    /* Running module_data_service here can synchronously send a notification
     * while NimBLE is handling the GATT write. Defer it out of host context. */
    uint8_t *copy = malloc(length);
    if (!copy) return BLE_ATT_ERR_INSUFFICIENT_RES;
    memcpy(copy, payload, length);
    const ble_rx_message_t message = {.payload = copy, .length = length};
    if (!s_rx_queue || xQueueSend(s_rx_queue, &message, 0) != pdTRUE) {
        free(copy);
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return 0;
}

static const struct ble_gatt_svc_def s_services[] = {
    { .type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &s_service_uuid.u,
      .characteristics = (struct ble_gatt_chr_def[]) {
          { .uuid = &s_write_uuid.u, .access_cb = access_cb, .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP },
          { .uuid = &s_notify_uuid.u, .access_cb = access_cb,
            .val_handle = &s_notify_handle, .flags = BLE_GATT_CHR_F_NOTIFY },
          { 0 }
      } },
    { 0 }
};

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
        s_connection = event->connect.status == 0 ? event->connect.conn_handle : BLE_HS_CONN_HANDLE_NONE;
        module_data_ble_reassembler_reset(&s_rx);
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        ESP_LOGW(TAG, "BLE disconnect reason=%d", event->disconnect.reason);
        s_connection = BLE_HS_CONN_HANDLE_NONE; module_data_ble_reassembler_reset(&s_rx);
        if (s_started) advertise();
    } else if (event->type == BLE_GAP_EVENT_NOTIFY_TX &&
               event->notify_tx.conn_handle == s_connection && event->notify_tx.attr_handle == s_notify_handle) {
        s_notify_status = event->notify_tx.status;
        if (s_notify_done) xSemaphoreGive(s_notify_done);
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)ble_svc_gap_device_name(); fields.name_len = strlen(ble_svc_gap_device_name()); fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);
    struct ble_gap_adv_params params = {0}; params.conn_mode = BLE_GAP_CONN_MODE_UND; params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &params, gap_event, NULL);
}

static void on_sync(void)
{
    const int rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        return;
    }
    advertise();
}

esp_err_t module_data_ble_gatt_start(const char *device_name, module_data_ble_rx_cb_t receive, void *context)
{
    if (!device_name || !device_name[0] || !receive || s_started) return ESP_ERR_INVALID_ARG;
    s_tx_lock = xSemaphoreCreateMutex(); if (!s_tx_lock) return ESP_ERR_NO_MEM;
    s_receive = receive; s_context = context; module_data_ble_reassembler_reset(&s_rx);
    s_rx_queue = xQueueCreate(4, sizeof(ble_rx_message_t));
    s_notify_done = xSemaphoreCreateBinary();
    if (!s_rx_queue || !s_notify_done ||
        xTaskCreate(ble_rx_task, "ble_rx_service", 8192, NULL, 5, &s_rx_task) != pdPASS) {
        if (s_rx_queue) { vQueueDelete(s_rx_queue); s_rx_queue = NULL; }
        if (s_notify_done) { vSemaphoreDelete(s_notify_done); s_notify_done = NULL; }
        s_receive = NULL; s_context = NULL; vSemaphoreDelete(s_tx_lock); s_tx_lock = NULL;
        return ESP_ERR_NO_MEM;
    }
    const esp_err_t init_result = nimble_port_init();
    if (init_result != ESP_OK) {
        vTaskDelete(s_rx_task); s_rx_task = NULL;
        vQueueDelete(s_rx_queue); s_rx_queue = NULL;
        vSemaphoreDelete(s_notify_done); s_notify_done = NULL;
        vSemaphoreDelete(s_tx_lock); s_tx_lock = NULL;
        s_receive = NULL; s_context = NULL;
        return init_result;
    }
    ble_svc_gap_init(); ble_svc_gatt_init();
    ble_svc_gap_device_name_set(device_name); ble_hs_cfg.sync_cb = on_sync;
    int rc = ble_gatts_count_cfg(s_services); if (rc) goto fail;
    rc = ble_gatts_add_svcs(s_services); if (rc) goto fail;
    s_started = true; nimble_port_freertos_init(nimble_host_task); ESP_LOGI(TAG, "BLE GATT service ready"); return ESP_OK;
fail:
    s_receive = NULL; s_context = NULL;
    if (s_rx_task) { vTaskDelete(s_rx_task); s_rx_task = NULL; }
    if (s_rx_queue) { vQueueDelete(s_rx_queue); s_rx_queue = NULL; }
    if (s_notify_done) { vSemaphoreDelete(s_notify_done); s_notify_done = NULL; }
    vSemaphoreDelete(s_tx_lock); s_tx_lock = NULL;
    nimble_port_deinit(); return ESP_FAIL;
}

void module_data_ble_gatt_stop(void)
{
    if (!s_started) return;
    if (s_connection != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(s_connection, BLE_ERR_REM_USER_CONN_TERM);
    s_connection = BLE_HS_CONN_HANDLE_NONE; s_receive = NULL; s_context = NULL; s_started = false;
    nimble_port_stop(); nimble_port_freertos_deinit(); nimble_port_deinit();
    if (s_rx_task) { vTaskDelete(s_rx_task); s_rx_task = NULL; }
    if (s_rx_queue) {
        ble_rx_message_t message;
        while (xQueueReceive(s_rx_queue, &message, 0) == pdTRUE) free(message.payload);
        vQueueDelete(s_rx_queue); s_rx_queue = NULL;
    }
    if (s_notify_done) { vSemaphoreDelete(s_notify_done); s_notify_done = NULL; }
    if (s_tx_lock) { vSemaphoreDelete(s_tx_lock); s_tx_lock = NULL; }
}

static bool emit_fragment(const uint8_t *fragment, size_t length, void *context)
{
    (void)context;
    if (s_connection == BLE_HS_CONN_HANDLE_NONE) { ESP_LOGW(TAG, "notify without BLE connection"); return false; }
    if (!s_notify_done) { ESP_LOGE(TAG, "notify completion semaphore missing"); return false; }
    int rc = BLE_HS_ENOMEM;
    for (unsigned attempt = 0; attempt < 24; ++attempt) {
        while (xSemaphoreTake(s_notify_done, 0) == pdTRUE) { }
        s_notify_status = BLE_HS_EBUSY;
        struct os_mbuf *om = ble_hs_mbuf_from_flat(fragment, length);
        if (!om) {
            if (attempt == 23) { ESP_LOGW(TAG, "notify mbuf allocation exhausted retries"); return false; }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        rc = ble_gatts_notify_custom(s_connection, s_notify_handle, om);
        if (rc == 0) break;
        if (rc != BLE_HS_ENOMEM) { ESP_LOGW(TAG, "notify enqueue failed rc=%d", rc); return false; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (rc != 0) { ESP_LOGW(TAG, "notify enqueue exhausted retries rc=%d", rc); return false; }
    /* Notifications are unacknowledged at ATT level. Wait for the NimBLE TX
     * completion event before enqueuing the next fragment to avoid overflowing
     * the controller queue on large signed package chunks. */
    if (xSemaphoreTake(s_notify_done, pdMS_TO_TICKS(3000)) != pdTRUE) {
        ESP_LOGW(TAG, "notify TX completion timeout");
        return false;
    }
    if (s_notify_status != 0) ESP_LOGW(TAG, "notify TX failed status=%d", s_notify_status);
    return s_notify_status == 0 && s_connection != BLE_HS_CONN_HANDLE_NONE;
}

bool module_data_ble_gatt_send(const uint8_t *payload, size_t length)
{
    if (!s_tx_lock || !s_started) return false;
    xSemaphoreTake(s_tx_lock, portMAX_DELAY);
    const bool ok = module_data_ble_encode(s_message_id++, payload, length, emit_fragment, NULL);
    xSemaphoreGive(s_tx_lock); return ok;
}

bool module_data_ble_gatt_send_callback(const uint8_t *payload, size_t length, void *context)
{
    (void)context; return module_data_ble_gatt_send(payload, length);
}
