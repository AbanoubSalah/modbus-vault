idf_component_register(SRCS "rs485_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_uart esp_timer runtime_tasks)
