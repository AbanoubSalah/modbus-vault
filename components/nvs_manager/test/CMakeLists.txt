cmake_minimum_required(VERSION 3.16)

idf_component_register(SRCS "test_nvs_manager.c"
    INCLUDE_DIRS "include"
    REQUIRES unity nvs_flash nvs_manager)