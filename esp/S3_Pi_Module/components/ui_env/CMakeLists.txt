idf_component_register(SRCS "ui_env.c" "ui_env_view.c"
                           "fonts/plant_zh_12.c" "fonts/plant_zh_14.c"
                           "fonts/plant_roboto_24.c" "fonts/plant_roboto_36.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl pi_sensor_device lcd_st7789 esp_timer freertos power_guard)
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE=1)
