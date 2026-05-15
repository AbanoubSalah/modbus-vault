cmake_minimum_required(VERSION 3.16)

idf_component_register(SRCS "test_serializer.c"
    REQUIRES unity serializer)
