cmake_minimum_required(VERSION 3.16)

idf_component_register(SRCS "test_blackbox_logger.c" "mock_partition.c"
    INCLUDE_DIRS "include"
    REQUIRES unity esp_timer blackbox_logger utils)
