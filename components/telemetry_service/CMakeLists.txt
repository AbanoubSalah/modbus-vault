idf_component_register(SRCS "telemetry_service.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt_bridge telemetry_pipeline metrics debug_pins)
