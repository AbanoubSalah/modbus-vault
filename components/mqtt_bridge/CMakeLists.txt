idf_component_register(SRCS "mqtt_bridge.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt blackbox_logger metrics)
