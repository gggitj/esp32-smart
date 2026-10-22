#include "module_data_service.h"
#include "module_control_service.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include <string.h>

#define MODULE_ID @MODULE_ID@
static const char *TAG = "module";

extern const uint8_t _binary_embedded_package_json_start[];
extern const uint8_t _binary_embedded_package_json_end[];
extern const uint8_t _binary_embedded_package_tar_zst_start[];
extern const uint8_t _binary_embedded_package_tar_zst_end[];

static size_t package_reader(const char *file, uint32_t offset, uint8_t *buffer,
                             size_t capacity, uint32_t *total, void *context) {
    (void)context;
    const uint8_t *start = NULL, *end = NULL;
    if (strcmp(file, "package.json") == 0) {
        start = _binary_embedded_package_json_start; end = _binary_embedded_package_json_end;
    } else if (strcmp(file, "package.tar.zst") == 0) {
        start = _binary_embedded_package_tar_zst_start; end = _binary_embedded_package_tar_zst_end;
    } else return 0;
    size_t size = (size_t)(end - start);
    if (!size || offset >= size || !buffer || !capacity || !total) return 0;
    *total = (uint32_t)size;
    if (capacity > size - offset) capacity = size - offset;
    memcpy(buffer, start + offset, capacity);
    return capacity;
}

static bool on_command(const char *event, const char *json, void *context) {
    (void)context;
    ESP_LOGI(TAG, "command event=%s payload=%s", event ? event : "", json ? json : "");
    // Replace this callback with the module's hardware command dispatcher.
    if (event && strcmp(event, "data") == 0)
        module_data_service_publish((const uint8_t *)"{\"status\":\"ok\"}", 15);
    return true;
}

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    module_data_config_t config = {
        .module_id = MODULE_ID,
        .module_instance = CONFIG_MODULE_INSTANCE,
        .token = CONFIG_MODULE_TOKEN,
        .port = CONFIG_MODULE_PORT,
    };
    module_data_service_set_package_reader(package_reader, NULL);
    const module_control_service_config_t control = {
        .module_id = MODULE_ID,
#if CONFIG_MODULE_TRANSPORT_BLE
        .transport_mode = "fixed_ble",
#else
        .transport_mode = "fixed_hotspot_lan",
#endif
        .package_transport = "data", .module_type = "module",
    };
    ESP_ERROR_CHECK(module_control_service_start(&control));
    ESP_ERROR_CHECK(module_data_service_start(&config, on_command, NULL));
#if CONFIG_MODULE_TRANSPORT_BLE
    ESP_ERROR_CHECK(module_data_service_start_ble(CONFIG_MODULE_BLE_NAME));
#else
    const module_data_wifi_config_t wifi = {
        .ssid = CONFIG_PI_HOTSPOT_SSID, .password = CONFIG_PI_HOTSPOT_PASSWORD,
        .max_retry = 10, .static_ip = CONFIG_PI_MODULE_STATIC_IP,
        .netmask = CONFIG_PI_MODULE_NETMASK, .gateway = CONFIG_PI_HOTSPOT_GATEWAY,
    };
    ESP_ERROR_CHECK(module_data_wifi_start(&wifi));
#endif
    ESP_LOGI(TAG, "module data service started");
}
