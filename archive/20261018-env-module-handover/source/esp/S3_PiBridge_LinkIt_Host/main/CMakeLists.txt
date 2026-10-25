idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES linkit_sdk board_pins nvs_flash driver json esp_timer
)
