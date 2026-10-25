if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
        REQUIRES linkit_sdk board_pins nvs_flash esp_driver_gpio module_data_service pi_sensor_device json mbedtls lcd_st7789 ui_env lvgl power_guard)
    return()
endif()
set(PI_MODULE_PACKAGE_DIR "${CMAKE_CURRENT_LIST_DIR}" CACHE PATH "Directory containing the signed embedded package files")
foreach(package_file embedded_package.json embedded_package.tar.zst)
    if(NOT EXISTS "${PI_MODULE_PACKAGE_DIR}/${package_file}")
        message(FATAL_ERROR "Provision ${PI_MODULE_PACKAGE_DIR}/${package_file} from a signed ARM64 package before building; never embed a signing private key")
    endif()
endforeach()
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES linkit_sdk board_pins nvs_flash esp_driver_gpio module_data_service pi_sensor_device json mbedtls lcd_st7789 ui_env lvgl power_guard
    EMBED_FILES "${PI_MODULE_PACKAGE_DIR}/embedded_package.json" "${PI_MODULE_PACKAGE_DIR}/embedded_package.tar.zst"
)
