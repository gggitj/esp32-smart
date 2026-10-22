idf_component_register(SRCS "pi_sensor_device.c" INCLUDE_DIRS "include"
    REQUIRES esp_driver_i2c freertos PRIV_REQUIRES esp_driver_gpio esp_timer)
