cmake_minimum_required(VERSION 3.16)

idf_component_register(SRCS "test_rs485_driver.c"
    REQUIRES unity rs485_driver esp_driver_uart)
