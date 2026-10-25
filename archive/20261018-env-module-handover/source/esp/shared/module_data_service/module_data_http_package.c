#include "module_data_service.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <errno.h>
#include <string.h>

#define PACKAGE_HTTP_LIMIT (32U * 1024U * 1024U)
#define PACKAGE_HTTP_CHUNK 1024U

static const char *TAG = "PI_PACKAGE_HTTP";
static httpd_handle_t s_httpd;
static uint32_t s_allowed_peer_ipv4;

extern bool module_data_service_package_reader_ready(void);
extern size_t module_data_service_read_package(const char *file, uint32_t offset,
                                               uint8_t *buffer, size_t capacity,
                                               uint32_t *total);

static esp_err_t serve_package(httpd_req_t *request)
{
    struct sockaddr_storage peer = {0};
    socklen_t peer_length = sizeof(peer);
    const int peer_result = getpeername(httpd_req_to_sockfd(request), (struct sockaddr *)&peer, &peer_length);
    uint32_t peer_ipv4 = 0;
    bool peer_is_allowed = false;
    if (peer_result == 0 && peer.ss_family == AF_INET && peer_length >= sizeof(struct sockaddr_in)) {
        const struct sockaddr_in *peer4 = (const struct sockaddr_in *)&peer;
        peer_ipv4 = peer4->sin_addr.s_addr;
        peer_is_allowed = peer_ipv4 == s_allowed_peer_ipv4;
    } else if (peer_result == 0 && peer.ss_family == AF_INET6 && peer_length >= sizeof(struct sockaddr_in6)) {
        const struct sockaddr_in6 *peer6 = (const struct sockaddr_in6 *)&peer;
        if (IN6_IS_ADDR_V4MAPPED(&peer6->sin6_addr)) {
            memcpy(&peer_ipv4, &peer6->sin6_addr.s6_addr[12], sizeof(peer_ipv4));
            peer_is_allowed = peer_ipv4 == s_allowed_peer_ipv4;
        }
    }
    if (!peer_is_allowed) {
        char peer_text[INET6_ADDRSTRLEN] = "unknown";
        char allowed_text[INET_ADDRSTRLEN] = "unknown";
        if (peer_result == 0 && peer.ss_family == AF_INET)
            inet_ntop(AF_INET, &((const struct sockaddr_in *)&peer)->sin_addr, peer_text, sizeof(peer_text));
        else if (peer_result == 0 && peer.ss_family == AF_INET6)
            inet_ntop(AF_INET6, &((const struct sockaddr_in6 *)&peer)->sin6_addr, peer_text, sizeof(peer_text));
        struct in_addr allowed = {.s_addr = s_allowed_peer_ipv4};
        inet_ntop(AF_INET, &allowed, allowed_text, sizeof(allowed_text));
        ESP_LOGW(TAG, "reject HTTP package peer=%s family=%d getpeername_errno=%d allowed_gateway=%s",
                 peer_text, (int)peer.ss_family, peer_result ? errno : 0, allowed_text);
        httpd_resp_send_err(request, HTTPD_403_FORBIDDEN, "package client is not the configured Pi hotspot gateway");
        return ESP_OK;
    }
    const char *name = NULL;
    uint32_t file_limit = 0;
    if (strcmp(request->uri, "/package.json") == 0) {
        name = "package.json";
        file_limit = 256U * 1024U;
        httpd_resp_set_type(request, "application/json");
    } else if (strcmp(request->uri, "/package.tar.zst") == 0) {
        name = "package.tar.zst";
        file_limit = PACKAGE_HTTP_LIMIT;
        httpd_resp_set_type(request, "application/octet-stream");
    } else {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "package file not found");
        return ESP_OK;
    }

    if (!module_data_service_package_reader_ready()) {
        httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "package reader unavailable");
        return ESP_OK;
    }

    uint8_t chunk[PACKAGE_HTTP_CHUNK];
    uint32_t total = 0;
    size_t count = module_data_service_read_package(name, 0, chunk, sizeof(chunk), &total);
    if (count == 0 || total == 0 || total > file_limit || count > total) {
        httpd_resp_send_err(request, HTTPD_404_NOT_FOUND, "signed package unavailable or exceeds limit");
        return ESP_OK;
    }
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send_chunk(request, (const char *)chunk, count);
    uint32_t offset = (uint32_t)count;
    while (err == ESP_OK && offset < total) {
        uint32_t current_total = 0;
        count = module_data_service_read_package(name, offset, chunk, sizeof(chunk), &current_total);
        if (count == 0 || current_total != total || count > total - offset) {
            ESP_LOGW(TAG, "package read failed file=%s offset=%lu", name, (unsigned long)offset);
            return ESP_FAIL;
        }
        err = httpd_resp_send_chunk(request, (const char *)chunk, count);
        offset += (uint32_t)count;
    }
    if (err == ESP_OK) err = httpd_resp_send_chunk(request, NULL, 0);
    if (err == ESP_OK)
        ESP_LOGI(TAG, "served signed package file=%s bytes=%lu", name, (unsigned long)total);
    return err;
}

esp_err_t module_data_service_start_package_http(uint16_t port, const char *allowed_peer_ipv4)
{
    struct in_addr allowed_peer = {0};
    if (!module_data_service_package_reader_ready() || port == 0 || port == UINT16_MAX ||
        !allowed_peer_ipv4 || inet_aton(allowed_peer_ipv4, &allowed_peer) == 0)
        return ESP_ERR_INVALID_STATE;
    if (s_httpd) return ESP_ERR_INVALID_STATE;

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.ctrl_port = (uint16_t)(port + 1);
    config.max_uri_handlers = 2;
    config.max_open_sockets = 2;
    config.stack_size = 6144;
    config.lru_purge_enable = true;
    s_allowed_peer_ipv4 = allowed_peer.s_addr;
    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) {
        s_httpd = NULL;
        ESP_LOGW(TAG, "HTTP package endpoint start failed: %s", esp_err_to_name(err));
        return err;
    }

    const httpd_uri_t metadata = {.uri = "/package.json", .method = HTTP_GET,
                                  .handler = serve_package, .user_ctx = NULL};
    const httpd_uri_t archive = {.uri = "/package.tar.zst", .method = HTTP_GET,
                                 .handler = serve_package, .user_ctx = NULL};
    err = httpd_register_uri_handler(s_httpd, &metadata);
    if (err == ESP_OK) err = httpd_register_uri_handler(s_httpd, &archive);
    if (err != ESP_OK) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        ESP_LOGW(TAG, "HTTP package routes registration failed: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "read-only signed package endpoint ready on TCP %u", (unsigned)port);
    return ESP_OK;
}

void module_data_service_stop_package_http(void)
{
    if (!s_httpd) return;
    httpd_handle_t server = s_httpd;
    s_httpd = NULL;
    httpd_stop(server);
}
