set(MODULE_EMBED_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
foreach(package_file embedded_package.json embedded_package.tar.zst)
    if(NOT EXISTS "${MODULE_EMBED_DIR}/${package_file}")
        message(FATAL_ERROR "Missing ${package_file}; run module-devkit/firmware-build.sh")
    endif()
endforeach()
idf_component_register(SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES module_data_service module_control_service nvs_flash esp_netif
    EMBED_FILES "${MODULE_EMBED_DIR}/embedded_package.json" "${MODULE_EMBED_DIR}/embedded_package.tar.zst")
