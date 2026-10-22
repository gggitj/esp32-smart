#include "module_data_service.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "nvs_flash.h"

#include <string.h>

#define WIFI_CONNECTED BIT0
#define WIFI_FAILED BIT1

static EventGroupHandle_t s_wifi_events;
static esp_netif_t *s_netif;
static int s_retry;
static int s_max_retry;
static bool s_static_ipv4;
static bool s_ever_connected;
static esp_timer_handle_t s_reconnect_timer;

static void reconnect_station(void *arg)
{
    (void)arg;
    if (s_wifi_events) esp_wifi_connect();
}

static void cleanup_wifi(void)
{
    if (s_reconnect_timer) {
        esp_timer_stop(s_reconnect_timer);
        esp_timer_delete(s_reconnect_timer);
        s_reconnect_timer = NULL;
    }
    if (s_wifi_events) { vEventGroupDelete(s_wifi_events); s_wifi_events = NULL; }
    if (s_netif) { esp_netif_destroy_default_wifi(s_netif); s_netif = NULL; }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) esp_wifi_connect();
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED && s_static_ipv4) {
        s_retry = 0; s_ever_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED);
    }
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_wifi_events, WIFI_CONNECTED);
        if (s_ever_connected && s_reconnect_timer) {
            esp_timer_stop(s_reconnect_timer);
            esp_timer_start_once(s_reconnect_timer, 2000000);
        } else if (s_retry++ < s_max_retry) esp_wifi_connect();
        else xEventGroupSetBits(s_wifi_events, WIFI_FAILED);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        s_retry = 0; s_ever_connected = true;
        xEventGroupSetBits(s_wifi_events, WIFI_CONNECTED);
    }
}

esp_err_t module_data_wifi_start(const module_data_wifi_config_t *config)
{
    if (!config || !config->ssid || !config->password || !config->ssid[0]) return ESP_ERR_INVALID_ARG;
    if (s_wifi_events) return ESP_ERR_INVALID_STATE;
    s_wifi_events = xEventGroupCreate(); if (!s_wifi_events) return ESP_ERR_NO_MEM;
    s_max_retry = config->max_retry ? config->max_retry : 10; s_retry = 0; s_ever_connected = false;
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { cleanup_wifi(); return err; }
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) { cleanup_wifi(); return err; }
    s_netif = esp_netif_create_default_wifi_sta(); if (!s_netif) { cleanup_wifi(); return ESP_ERR_NO_MEM; }
    const bool has_static_ip = config->static_ip && config->static_ip[0];
    if (has_static_ip) {
        if (!config->netmask || !config->netmask[0] || !config->gateway || !config->gateway[0]) {
            cleanup_wifi(); return ESP_ERR_INVALID_ARG;
        }
        esp_netif_ip_info_t ip_info = {0};
        err = esp_netif_dhcpc_stop(s_netif);
        if (err != ESP_OK && err != ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED) { cleanup_wifi(); return err; }
        if ((err = esp_netif_str_to_ip4(config->static_ip, &ip_info.ip)) != ESP_OK ||
            (err = esp_netif_str_to_ip4(config->netmask, &ip_info.netmask)) != ESP_OK ||
            (err = esp_netif_str_to_ip4(config->gateway, &ip_info.gw)) != ESP_OK ||
            (err = esp_netif_set_ip_info(s_netif, &ip_info)) != ESP_OK) { cleanup_wifi(); return err; }
    }
    s_static_ipv4 = has_static_ip;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    if ((err = esp_wifi_init(&init)) != ESP_OK) { cleanup_wifi(); return err; }
    const esp_timer_create_args_t timer_config = {.callback = reconnect_station, .name = "module_wifi_retry"};
    if ((err = esp_timer_create(&timer_config, &s_reconnect_timer)) != ESP_OK) { esp_wifi_deinit(); cleanup_wifi(); return err; }
    if ((err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL)) != ESP_OK) { esp_wifi_deinit(); cleanup_wifi(); return err; }
    if ((err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL)) != ESP_OK) { esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event); esp_wifi_deinit(); cleanup_wifi(); return err; }
    wifi_config_t wifi = {0};
    strlcpy((char *)wifi.sta.ssid, config->ssid, sizeof(wifi.sta.ssid));
    strlcpy((char *)wifi.sta.password, config->password, sizeof(wifi.sta.password));
    // Accept open APs only when explicitly configured without a password.
    wifi.sta.threshold.authmode = config->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    if ((err = esp_wifi_set_mode(WIFI_MODE_STA)) != ESP_OK || (err = esp_wifi_set_config(WIFI_IF_STA, &wifi)) != ESP_OK || (err = esp_wifi_start()) != ESP_OK) {
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event); esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event); esp_wifi_deinit(); cleanup_wifi(); return err;
    }
    const EventBits_t bits = xEventGroupWaitBits(s_wifi_events, WIFI_CONNECTED | WIFI_FAILED, pdFALSE, pdFALSE, pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED) return ESP_OK;
    module_data_wifi_stop(); return ESP_ERR_TIMEOUT;
}

void module_data_wifi_stop(void)
{
    if (!s_wifi_events) return;
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event);
    esp_wifi_disconnect(); esp_wifi_stop(); esp_wifi_deinit(); cleanup_wifi();
    s_static_ipv4 = false;
}
