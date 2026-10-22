idf_component_register(SRCS "ui_env.c"
                       INCLUDE_DIRS "include"
                       REQUIRES lvgl pi_sensor_device lcd_st7789 esp_timer freertos)
