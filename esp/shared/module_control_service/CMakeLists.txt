idf_component_register(
    SRCS "module_control_service.c"
    INCLUDE_DIRS "include"
    REQUIRES linkit_sdk board_pins driver freertos)
