# LVGL 8.4.0 runtime subset, vendored with its original licence.
file(GLOB_RECURSE LVGL_SOURCES "${CMAKE_CURRENT_LIST_DIR}/src/*.c")
idf_component_register(SRCS ${LVGL_SOURCES} INCLUDE_DIRS "." "src" REQUIRES esp_timer)
target_compile_definitions(${COMPONENT_LIB} PUBLIC LV_CONF_INCLUDE_SIMPLE)
