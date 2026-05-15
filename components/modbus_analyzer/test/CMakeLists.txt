cmake_minimum_required(VERSION 3.16)

idf_component_register(SRCS "test_modbus_slicer.c"
    REQUIRES unity modbus_analyzer)
