set(MODULE_EMBED_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
foreach(package_file embedded_package.json embedded_package.tar.zst)
    if(NOT EXISTS "${MODULE_EMBED_DIR}/${package_file}")
        message(FATAL_ERROR "Missing ${package_file}; run module-devkit/firmware-build.sh")
    endif()
endforeach()
idf_component_register(SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES module_data_service linkit_sdk board_pins nvs_flash esp_netif esp_driver_gpio
             pi_sensor_device json mbedtls lcd_st7789 ui_env lvgl power_guard
    EMBED_FILES "${MODULE_EMBED_DIR}/embedded_package.json" "${MODULE_EMBED_DIR}/embedded_package.tar.zst")
