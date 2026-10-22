idf_component_register(SRCS "power_guard.c"
                       INCLUDE_DIRS "include"
                       REQUIRES pi_sensor_device lcd_st7789 freertos module_data_service esp_timer driver)
