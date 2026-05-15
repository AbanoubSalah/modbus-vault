idf_component_register(SRCS "modbus_slicer.c" "modbus_parser.c" "modbus_analyzer.c"
    INCLUDE_DIRS "include"
    REQUIRES rs485_driver slab_pool event_bus runtime_tasks metrics utils debug_pins)
