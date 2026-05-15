cmake_minimum_required(VERSION 3.16)

# Set the components directory to point back to the main project
set(EXTRA_COMPONENT_DIRS "../components")
set(TEST_COMPONENTS rs485_driver modbus_analyzer blackbox_logger nvs_manager mqtt_bridge serializer)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test_modbus_vault)
