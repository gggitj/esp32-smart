#include "rfid_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "sdkconfig.h"
#include "bridge_pins.h"
#include "rfid_tracker.h"
#include "cJSON.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;
static rfid_status_t s_status;

static void set_status(rfid_status_t status)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status = status;
    portEXIT_CRITICAL(&s_status_lock);
}

rfid_status_t rfid_service_status(void)
{
    portENTER_CRITICAL(&s_status_lock);
    rfid_status_t result = s_status;
    portEXIT_CRITICAL(&s_status_lock);
    return result;
}

#if CONFIG_BRIDGE_RFID_ENABLED
static rfid_send_fn s_send;
static bool s_started;
static char s_boot_id[17];

static bool report_card(const rc522_uid_t *uid)
{
    char hex[21];
    if (!rc522_uid_hex(uid, hex, sizeof(hex))) return false;
    cJSON *event = cJSON_CreateObject();
    if (!event) return false;
    bool ok = cJSON_AddStringToObject(event, "event", "rfid_card") &&
        cJSON_AddStringToObject(event, "schema", "terminal-rfid-v1") &&
        cJSON_AddStringToObject(event, "reader_id", CONFIG_BRIDGE_RFID_READER_ID) &&
        cJSON_AddStringToObject(event, "boot_id", s_boot_id) &&
        cJSON_AddStringToObject(event, "uid", hex) &&
        cJSON_AddNumberToObject(event, "uid_len", uid->length) &&
        cJSON_AddNumberToObject(event, "uptime_ms", esp_timer_get_time() / 1000);
    char *json = ok ? cJSON_PrintUnformatted(event) : NULL;
    ok = json && s_send(1, json);
    cJSON_free(json);
    cJSON_Delete(event);
    return ok;
}

static void reader_task(void *arg)
{
    (void)arg;
    rfid_status_t status = {.enabled = true};
    rc522_t reader;
    status.last_error = rc522_open(&reader, CONFIG_BRIDGE_RFID_SS_GPIO,
        CONFIG_BRIDGE_RFID_SCK_GPIO, CONFIG_BRIDGE_RFID_MOSI_GPIO,
        CONFIG_BRIDGE_RFID_MISO_GPIO, CONFIG_BRIDGE_RFID_RST_GPIO);
    set_status(status);
    if (status.last_error != ESP_OK) { vTaskDelete(NULL); return; }

    rfid_tracker_t tracker;
    rfid_tracker_init(&tracker);
    bool configured = false;
    int64_t retry_at_ms = 0;
    for (;;) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (!configured) {
            if (now_ms >= retry_at_ms) {
                status.last_error = rc522_reset(&reader);
                configured = status.last_error == ESP_OK;
                status.online = configured;
                status.version = reader.version;
                retry_at_ms = esp_timer_get_time() / 1000 + 2000;
                set_status(status);
            }
        } else {
            rc522_uid_t uid;
            bool present;
            esp_err_t err = rc522_poll(&reader, &uid, &present);
            now_ms = esp_timer_get_time() / 1000;
            if (err == ESP_OK) {
                status.last_error = ESP_OK;
                rfid_tracker_present(&tracker);
                if (rfid_tracker_should_report(&tracker, &uid)) {
                    if (report_card(&uid)) {
                        rfid_tracker_reported(&tracker, &uid);
                        ++status.reports;
                    } else ++status.tx_failures;
                }
            } else if (err == ESP_ERR_NOT_FOUND && !present) {
                status.last_error = ESP_OK; /* Healthy reader, no card. */
                rfid_tracker_absent(&tracker, now_ms, CONFIG_BRIDGE_RFID_REMOVAL_MS);
            } else {
                ++status.read_errors;
                status.last_error = err;
                /* Errors and reader loss cannot establish physical card removal. */
                rfid_tracker_present(&tracker);
                if (!present || (err != ESP_ERR_INVALID_RESPONSE && err != ESP_ERR_INVALID_CRC &&
                                 err != ESP_ERR_NOT_FOUND)) {
                    configured = false;
                    status.online = false;
                    retry_at_ms = now_ms + 2000;
                }
            }
            set_status(status);
        }
        vTaskDelay(pdMS_TO_TICKS(CONFIG_BRIDGE_RFID_POLL_MS));
    }
}
#endif

esp_err_t rfid_service_start(rfid_send_fn send)
{
#if CONFIG_BRIDGE_RFID_ENABLED
    if (!send) return ESP_ERR_INVALID_ARG;
    if (s_started) return ESP_ERR_INVALID_STATE;
    /* JSON escaping is handled by cJSON; cap the ID to keep event size bounded. */
    if (!CONFIG_BRIDGE_RFID_READER_ID[0] || sizeof(CONFIG_BRIDGE_RFID_READER_ID) > 65)
        return ESP_ERR_INVALID_ARG;
    s_send = send;
    snprintf(s_boot_id, sizeof(s_boot_id), "%08" PRIx32 "%08" PRIx32, esp_random(), esp_random());
    set_status((rfid_status_t){.enabled = true});
    if (xTaskCreate(reader_task, "rc522_reader", 6144, NULL, 3, NULL) != pdPASS) {
        set_status((rfid_status_t){.enabled = true, .last_error = ESP_ERR_NO_MEM});
        return ESP_ERR_NO_MEM;
    }
    s_started = true;
#else
    (void)send;
    set_status((rfid_status_t){0});
#endif
    return ESP_OK;
}
