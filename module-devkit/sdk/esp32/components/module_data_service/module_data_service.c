#include "module_data_service.h"

#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "module_data_ble_gatt.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_JSON 1048572U
#define MAX_FRAME (MAX_JSON + 8U)
#define RX_TIMEOUT_SEC 10

void module_data_service_stop_package_http(void);

static const char *TAG = "PI_MODULE_DATA";
static module_data_config_t s_cfg;
static char s_instance[65], s_token[257];
static module_data_command_cb_t s_command;
static void *s_context;
static module_data_package_read_cb_t s_package_reader;
static void *s_package_context;
static TaskHandle_t s_task;
static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_done;
static int s_server = -1;
static int s_client = -1;
static uint32_t s_tx = 1;
static uint32_t s_rx = 0;
static bool s_ready;
static bool s_disconnect_pending;
static volatile bool s_stop_requested;
static module_data_wire_send_cb_t s_ble_sender;
static void *s_ble_context;
static bool s_ble_active;
static char s_package_transfer[80];
static char s_package_file[24];

bool module_data_service_package_reader_ready(void)
{
    if (!s_lock) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool ready = s_package_reader != NULL;
    xSemaphoreGive(s_lock);
    return ready;
}

size_t module_data_service_read_package(const char *file, uint32_t offset,
                                        uint8_t *buffer, size_t capacity,
                                        uint32_t *total)
{
    if (!s_lock) return 0;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    module_data_package_read_cb_t reader = s_package_reader;
    void *context = s_package_context;
    xSemaphoreGive(s_lock);
    return reader ? reader(file, offset, buffer, capacity, total, context) : 0;
}

static void ble_receive(const uint8_t *payload, size_t length, void *context)
{
    (void)context;
    ESP_LOGI(TAG, "BLE reassembled payload bytes=%u", (unsigned)length);
    module_data_service_receive_ble(payload, length);
}

static uint32_t crc32(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ (0xedb88320U & (-(int32_t)(crc & 1U)));
    }
    return ~crc;
}

static bool send_all(int fd, const uint8_t *data, size_t length)
{
    while (length) {
        const int n = send(fd, data, length, 0);
        if (n <= 0) return false;
        data += n; length -= (size_t)n;
    }
    return true;
}

static bool send_json_locked(const char *event, cJSON *body)
{
    if (!s_ready || !body || !s_tx) return false;
    cJSON_AddStringToObject(body, "event", event);
    cJSON_AddNumberToObject(body, "version", 1);
    cJSON_AddNumberToObject(body, "module_id", s_cfg.module_id);
    cJSON_AddStringToObject(body, "module_instance", s_instance);
    cJSON_AddNumberToObject(body, "seq", (double)s_tx++);
    char *text = cJSON_PrintUnformatted(body);
    if (!text) return false;
    const size_t length = strlen(text);
    bool ok = length <= MAX_JSON;
    if (ok && s_ble_active && s_ble_sender) ok = s_ble_sender((const uint8_t *)text, length, s_ble_context);
    if (s_ble_active) { cJSON_free(text); return ok; }
    uint8_t header[4];
    if (ok) {
        const uint32_t wire_length = (uint32_t)length + 4U;
        header[0] = (uint8_t)(wire_length >> 24); header[1] = (uint8_t)(wire_length >> 16);
        header[2] = (uint8_t)(wire_length >> 8); header[3] = (uint8_t)wire_length;
        uint8_t crc[4]; const uint32_t sum = crc32((const uint8_t *)text, length);
        crc[0] = (uint8_t)sum; crc[1] = (uint8_t)(sum >> 8); crc[2] = (uint8_t)(sum >> 16); crc[3] = (uint8_t)(sum >> 24);
        ok = send_all(s_client, header, sizeof(header)) && send_all(s_client, (const uint8_t *)text, length) && send_all(s_client, crc, sizeof(crc));
    }
    cJSON_free(text); return ok;
}

static bool sequence_ok(cJSON *object, uint32_t *out, bool new_session)
{
    const cJSON *seq = cJSON_GetObjectItemCaseSensitive(object, "seq");
    if (!cJSON_IsNumber(seq) || seq->valuedouble < 1 || seq->valuedouble > 4294967295.0 || seq->valuedouble != (uint32_t)seq->valuedouble) return false;
    const uint32_t value = (uint32_t)seq->valuedouble;
    if (!new_session && s_rx && (value - s_rx == 0 || value - s_rx >= 0x80000000U)) return false;
    *out = value; return true;
}

static bool read_full(int fd, uint8_t *buffer, size_t length)
{
    while (length) {
        const int n = recv(fd, buffer, length, 0);
        if (n <= 0) return false;
        buffer += n; length -= (size_t)n;
    }
    return true;
}

static void handle_json(char *text, bool ble)
{
    cJSON *object = cJSON_ParseWithLength(text, strlen(text));
    if (!object) { ESP_LOGW(TAG, "reject malformed JSON data event"); return; }
    uint32_t seq = 0;
    const cJSON *event = cJSON_GetObjectItemCaseSensitive(object, "event");
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(object, "version");
    const cJSON *module = cJSON_GetObjectItemCaseSensitive(object, "module_id");
    const cJSON *instance = cJSON_GetObjectItemCaseSensitive(object, "module_instance");
    const bool is_session_open = cJSON_IsString(event) && strcmp(event->valuestring, "session_open") == 0;
    if (!cJSON_IsString(event) || !cJSON_IsNumber(version) || version->valuedouble != 1 || !cJSON_IsNumber(module) || module->valuedouble != s_cfg.module_id ||
        !cJSON_IsString(instance) || strcmp(instance->valuestring, s_instance) != 0) {
        ESP_LOGW(TAG, "reject %s due to protocol identity/sequence", cJSON_IsString(event) ? event->valuestring : "unknown event");
        cJSON_Delete(object); return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool was_ready = s_ready;
    const bool valid_sequence = sequence_ok(object, &seq, is_session_open);
    const bool wrong_transport = !is_session_open && (!s_ready || s_ble_active != ble);
    if (!valid_sequence || wrong_transport) {
        xSemaphoreGive(s_lock); cJSON_Delete(object); return;
    }
    if (!is_session_open) s_rx = seq;
    xSemaphoreGive(s_lock);
    if (strcmp(event->valuestring, "session_open") == 0) {
        const cJSON *token = cJSON_GetObjectItemCaseSensitive(object, "token");
        // Reject unauthenticated opens without modifying an existing session.
        if (!cJSON_IsString(token) || strcmp(token->valuestring, s_token) != 0) { cJSON_Delete(object); return; }
        const cJSON *choice = cJSON_GetObjectItemCaseSensitive(object, "transport_choice");
        if (choice && (!cJSON_IsString(choice) || strcmp(choice->valuestring, ble ? "ble" : "hotspot_lan") != 0)) {
            cJSON_Delete(object); return;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_ready = true; s_ble_active = ble; s_rx = seq; s_tx = 1;
        s_disconnect_pending = false;
        s_package_transfer[0] = '\0'; s_package_file[0] = '\0';
        ESP_LOGI(TAG, "session_open transport=%s seq=%lu token_match=%d",
                 s_ble_active ? "BLE" : "LAN", (unsigned long)seq, s_ready);
        if (s_ready) {
            cJSON *reply = cJSON_CreateObject(); cJSON_AddStringToObject(reply, "token", s_token);
            cJSON_AddStringToObject(reply, "selected_transport", ble ? "ble" : "hotspot_lan");
            const bool sent = send_json_locked("session_ready", reply); xSemaphoreGive(s_lock);
            ESP_LOGI(TAG, "session_ready response transport=%s sent=%d", s_ble_active ? "BLE" : "LAN", sent);
            cJSON_Delete(reply);
        }
    } else if (was_ready) {
        if (strcmp(event->valuestring, "ping") == 0) {
            cJSON *reply = cJSON_CreateObject(); xSemaphoreTake(s_lock, portMAX_DELAY); send_json_locked("pong", reply); xSemaphoreGive(s_lock); cJSON_Delete(reply);
        }
        else if (strcmp(event->valuestring, "disconnect_ack") == 0) {
            const cJSON *accepted = cJSON_GetObjectItemCaseSensitive(object, "accepted");
            if (!s_disconnect_pending) { cJSON_Delete(object); return; }
            if (cJSON_IsBool(accepted) && cJSON_IsTrue(accepted)) {
                cJSON *reply = cJSON_CreateObject();
                xSemaphoreTake(s_lock, portMAX_DELAY);
                const bool sent = send_json_locked("session_close", reply);
                s_ready = false;
                s_disconnect_pending = false;
                xSemaphoreGive(s_lock);
                ESP_LOGI(TAG, "module disconnect accepted; session_close sent=%d", sent);
                cJSON_Delete(reply);
            } else {
                xSemaphoreTake(s_lock, portMAX_DELAY); s_disconnect_pending = false; xSemaphoreGive(s_lock);
                ESP_LOGI(TAG, "module disconnect request rejected by host");
            }
        }
        else if (strcmp(event->valuestring, "package_get") == 0 && s_package_reader) {
            const cJSON *transfer = cJSON_GetObjectItemCaseSensitive(object, "transfer_id");
            const cJSON *file = cJSON_GetObjectItemCaseSensitive(object, "file");
            const cJSON *offset = cJSON_GetObjectItemCaseSensitive(object, "offset");
            const cJSON *requested = cJSON_GetObjectItemCaseSensitive(object, "length");
            uint8_t bytes[512]; uint32_t total = 0;
            const bool valid_file = cJSON_IsString(file) && (!strcmp(file->valuestring, "package.json") || !strcmp(file->valuestring, "package.tar.zst"));
            const bool valid_numbers = cJSON_IsNumber(offset) && cJSON_IsNumber(requested) &&
                offset->valuedouble >= 0 && offset->valuedouble <= UINT32_MAX && offset->valuedouble == (uint32_t)offset->valuedouble &&
                requested->valuedouble > 0 && requested->valuedouble <= sizeof(bytes) && requested->valuedouble == (size_t)requested->valuedouble;
            const bool new_transfer = cJSON_IsString(transfer) && valid_file && valid_numbers && offset->valuedouble == 0;
            const bool same_transfer = cJSON_IsString(transfer) && valid_file && valid_numbers && !strcmp(transfer->valuestring, s_package_transfer) && !strcmp(file->valuestring, s_package_file);
            if (cJSON_IsString(transfer) && valid_file && valid_numbers && (new_transfer || same_transfer)) {
                if (new_transfer) { strlcpy(s_package_transfer, transfer->valuestring, sizeof(s_package_transfer)); strlcpy(s_package_file, file->valuestring, sizeof(s_package_file)); }
                const size_t package_capacity = s_ble_active && requested->valuedouble > 128 ? 128 : (size_t)requested->valuedouble;
                const size_t count = s_package_reader(file->valuestring, (uint32_t)offset->valuedouble, bytes,
                                                        package_capacity, &total, s_package_context);
                if (count > 0 && count <= sizeof(bytes) && count <= package_capacity && total > 0 &&
                    (uint32_t)offset->valuedouble <= total && count <= total - (uint32_t)offset->valuedouble) {
                    ESP_LOGI(TAG, "package_get transport=%s file=%s offset=%lu bytes=%u total=%lu",
                             s_ble_active ? "BLE" : "LAN", file->valuestring,
                             (unsigned long)offset->valuedouble, (unsigned)count, (unsigned long)total);
                    size_t encoded_length = 0; char encoded[sizeof(bytes) * 2];
                    if (mbedtls_base64_encode((unsigned char *)encoded, sizeof(encoded), &encoded_length, bytes, count) == 0) {
                        encoded[encoded_length] = '\0'; cJSON *reply = cJSON_CreateObject();
                        cJSON_AddStringToObject(reply, "transfer_id", transfer->valuestring);
                        cJSON_AddStringToObject(reply, "file", file->valuestring);
                        cJSON_AddNumberToObject(reply, "offset", (double)offset->valuedouble);
                        cJSON_AddNumberToObject(reply, "total", (double)total);
                        cJSON_AddStringToObject(reply, "payload", encoded);
                        xSemaphoreTake(s_lock, portMAX_DELAY);
                        const bool sent = send_json_locked("package_chunk", reply);
                        xSemaphoreGive(s_lock);
                        ESP_LOGI(TAG, "package_chunk transport=%s file=%s offset=%lu bytes=%u sent=%d",
                                 s_ble_active ? "BLE" : "LAN", file->valuestring,
                                 (unsigned long)offset->valuedouble, (unsigned)count, sent);
                        cJSON_Delete(reply);
                    }
                }
            }
        }
        else if (strcmp(event->valuestring, "session_close") == 0) {
            cJSON *reply = cJSON_CreateObject();
            xSemaphoreTake(s_lock, portMAX_DELAY); send_json_locked("session_close", reply); s_ready = false; xSemaphoreGive(s_lock);
            cJSON_Delete(reply);
        } else if (strcmp(event->valuestring, "stop") == 0) {
            if (s_command) s_command(event->valuestring, text, s_context);
            cJSON *reply = cJSON_CreateObject();
            xSemaphoreTake(s_lock, portMAX_DELAY); send_json_locked("session_close", reply); s_ready = false; xSemaphoreGive(s_lock);
            cJSON_Delete(reply);
        } else if (s_command) s_command(event->valuestring, text, s_context);
    }
    cJSON_Delete(object);
}

void module_data_service_receive(const uint8_t *payload, size_t length)
{
    if (!payload || !length || length > MAX_JSON || !s_lock || s_stop_requested || memchr(payload, '\0', length)) return;
    char *text = calloc(1, length + 1); if (!text) return;
    memcpy(text, payload, length); handle_json(text, false); free(text);
}

void module_data_service_receive_ble(const uint8_t *payload, size_t length)
{
    if (!payload || !length || length > MAX_JSON || !s_lock || s_stop_requested || memchr(payload, '\0', length)) return;
    char *text = calloc(1, length + 1); if (!text) return;
    memcpy(text, payload, length); handle_json(text, true); free(text);
}

static void service_task(void *arg)
{
    (void)arg;
    const int server = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    s_server = server;
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(s_cfg.port), .sin_addr.s_addr = htonl(INADDR_ANY)};
    int yes = 1; setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    if (server < 0 || bind(server, (struct sockaddr *)&address, sizeof(address)) != 0 || listen(server, 1) != 0) {
        ESP_LOGE(TAG, "TCP service setup failed");
        if (server >= 0) close(server);
        s_server = -1; s_task = NULL;
        if (s_done) xEventGroupSetBits(s_done, BIT0);
        vTaskDelete(NULL); return;
    }
    struct timeval timeout = {.tv_sec = RX_TIMEOUT_SEC, .tv_usec = 0};
    while (s_task && !s_stop_requested) {
        int client = accept(server, NULL, NULL); if (client < 0) continue;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        xSemaphoreTake(s_lock, portMAX_DELAY); s_client = client; xSemaphoreGive(s_lock);
        while (s_task && !s_stop_requested) {
            uint8_t header[4]; if (!read_full(client, header, sizeof(header))) break;
            const uint32_t length = ((uint32_t)header[0] << 24) | ((uint32_t)header[1] << 16) | ((uint32_t)header[2] << 8) | header[3];
            if (length < 4 || length > MAX_JSON + 4) break;
            char *payload = calloc(1, length - 3); uint8_t crc[4];
            if (!payload || !read_full(client, (uint8_t *)payload, length - 4) || !read_full(client, crc, sizeof(crc))) { free(payload); break; }
            const uint32_t expected = (uint32_t)crc[0] | ((uint32_t)crc[1] << 8) | ((uint32_t)crc[2] << 16) | ((uint32_t)crc[3] << 24);
            if (crc32((uint8_t *)payload, length - 4) == expected) module_data_service_receive((uint8_t *)payload, length - 4);
            free(payload);
        }
        shutdown(client, SHUT_RDWR); close(client);
        xSemaphoreTake(s_lock, portMAX_DELAY); s_client = -1;
        if (!s_ble_active) { s_ready = false; s_disconnect_pending = false; }
        xSemaphoreGive(s_lock);
    }
    close(server); s_server = -1; s_task = NULL; if (s_done) xEventGroupSetBits(s_done, BIT0); vTaskDelete(NULL);
}

esp_err_t module_data_service_start(const module_data_config_t *config, module_data_command_cb_t callback, void *context)
{
    if (!config || !config->module_id || !config->module_instance || !config->module_instance[0] ||
        !config->token || !config->token[0] || !config->port || s_task || s_lock) {
        ESP_LOGE(TAG, "start rejected config=%d module_id=%u instance=%d token=%d port=%u task=%d lock=%d",
                 config != NULL, config ? (unsigned)config->module_id : 0U,
                 config && config->module_instance && config->module_instance[0],
                 config && config->token && config->token[0],
                 config ? (unsigned)config->port : 0U, s_task != NULL, s_lock != NULL);
        return ESP_ERR_INVALID_ARG;
    }
    if (strlen(config->module_instance) > 64 || strlen(config->token) > 256) return ESP_ERR_INVALID_SIZE;
    s_cfg = *config; strlcpy(s_instance, config->module_instance, sizeof(s_instance)); strlcpy(s_token, config->token, sizeof(s_token));
    s_command = callback; s_context = context; s_stop_requested = false;
    s_ready = false; s_disconnect_pending = false; s_rx = 0; s_tx = 1; s_ble_active = false;
    s_lock = xSemaphoreCreateMutex(); s_done = xEventGroupCreate();
    if (!s_lock || !s_done || xTaskCreate(service_task, "pi_module_tcp", 8192, NULL, 5, &s_task) != pdPASS) {
        if (s_lock) { vSemaphoreDelete(s_lock); s_lock = NULL; }
        if (s_done) { vEventGroupDelete(s_done); s_done = NULL; }
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void module_data_service_stop(void)
{
    if (!s_lock) return;
    s_stop_requested = true;
    module_data_service_stop_package_http();
    module_data_ble_gatt_stop(); s_ble_sender = NULL; s_ble_context = NULL; s_ble_active = false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_client >= 0) shutdown(s_client, SHUT_RDWR);
    if (s_server >= 0) shutdown(s_server, SHUT_RDWR);
    xSemaphoreGive(s_lock);
    const EventBits_t stopped = s_done ? xEventGroupWaitBits(s_done, BIT0, pdTRUE, pdTRUE, pdMS_TO_TICKS(12000)) : 0;
    if (!(stopped & BIT0)) return;
    if (s_lock) { vSemaphoreDelete(s_lock); s_lock = NULL; }
    if (s_done) { vEventGroupDelete(s_done); s_done = NULL; }
}

esp_err_t module_data_service_start_ble(const char *device_name)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    esp_err_t err = module_data_ble_gatt_start(device_name, ble_receive, NULL);
    if (err == ESP_OK) module_data_service_set_ble_sender(module_data_ble_gatt_send_callback, NULL);
    return err;
}

void module_data_service_set_package_reader(module_data_package_read_cb_t reader, void *context)
{
    if (!s_lock) { s_package_reader = reader; s_package_context = context; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY); s_package_reader = reader; s_package_context = context; xSemaphoreGive(s_lock);
}

void module_data_service_set_ble_sender(module_data_wire_send_cb_t sender, void *context)
{
    if (!s_lock) { s_ble_sender = sender; s_ble_context = context; s_ble_active = sender != NULL; return; }
    xSemaphoreTake(s_lock, portMAX_DELAY); s_ble_sender = sender; s_ble_context = context; s_ble_active = sender != NULL; xSemaphoreGive(s_lock);
}

esp_err_t module_data_service_publish(const uint8_t *payload, size_t length)
{
    if ((!payload && length) || length > 65532U || !s_lock || s_stop_requested) return ESP_ERR_INVALID_SIZE;
    xSemaphoreTake(s_lock, portMAX_DELAY); if (!s_ready) { xSemaphoreGive(s_lock); return ESP_ERR_INVALID_STATE; }
    size_t encoded_length = 0; const size_t capacity = ((length + 2) / 3) * 4 + 1; char *encoded = malloc(capacity);
    if (!encoded || mbedtls_base64_encode((unsigned char *)encoded, capacity, &encoded_length, payload, length) != 0) { free(encoded); xSemaphoreGive(s_lock); return ESP_ERR_NO_MEM; }
    cJSON *body = cJSON_CreateObject();
    if (!body || !cJSON_AddStringToObject(body, "payload", encoded)) {
        free(encoded); cJSON_Delete(body); xSemaphoreGive(s_lock); return ESP_ERR_NO_MEM;
    }
    free(encoded);
    const bool ok = send_json_locked("data", body); cJSON_Delete(body); xSemaphoreGive(s_lock); return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t module_data_service_request_disconnect(const char *reason, bool auto_reconnect)
{
    static const char *const allowed[] = {"user_button", "low_battery", "fault", "enter_sleep", "reboot", "maintenance"};
    if (!reason || !s_lock) { ESP_LOGW(TAG, "disconnect request invalid argument/service state"); return ESP_ERR_INVALID_ARG; }
    bool valid = false;
    for (size_t i = 0; i < sizeof(allowed) / sizeof(allowed[0]); ++i)
        if (strcmp(reason, allowed[i]) == 0) { valid = true; break; }
    if (!valid) return ESP_ERR_INVALID_ARG;
    cJSON *body = cJSON_CreateObject();
    if (!body) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(body, "reason", reason);
    cJSON_AddBoolToObject(body, "stop_required", true);
    cJSON_AddBoolToObject(body, "auto_reconnect", auto_reconnect);
    ESP_LOGI(TAG, "queueing module disconnect request");
    xSemaphoreTake(s_lock, portMAX_DELAY);
    ESP_LOGI(TAG, "module disconnect request acquired service lock");
    if (!s_ready || s_disconnect_pending) {
        ESP_LOGW(TAG, "disconnect request rejected locally ready=%d pending=%d", s_ready, s_disconnect_pending);
        xSemaphoreGive(s_lock); cJSON_Delete(body); return ESP_ERR_INVALID_STATE;
    }
    s_disconnect_pending = true;
    const bool sent = send_json_locked("module_disconnect_request", body);
    if (!sent) s_disconnect_pending = false;
    xSemaphoreGive(s_lock);
    cJSON_Delete(body);
    ESP_LOGI(TAG, "module_disconnect_request reason=%s auto_reconnect=%d sent=%d", reason, auto_reconnect, sent);
    return sent ? ESP_OK : ESP_ERR_INVALID_STATE;
}
