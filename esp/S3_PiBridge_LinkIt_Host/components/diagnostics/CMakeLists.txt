idf_component_register(
    SRCS
        "diagnostics.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        board_pins
        driver
        esp_timer
)

