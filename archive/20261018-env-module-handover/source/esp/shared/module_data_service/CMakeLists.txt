idf_component_register(
    SRCS "module_data_service.c" "module_data_ble.c" "module_data_ble_gatt.c" "module_data_wifi.c" "module_data_package.c" "module_data_http_package.c"
    INCLUDE_DIRS "include"
    REQUIRES json freertos
    PRIV_REQUIRES lwip mbedtls esp_wifi esp_netif esp_event esp_timer nvs_flash bt esp_http_server
)
