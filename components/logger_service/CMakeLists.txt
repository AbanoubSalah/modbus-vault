idf_component_register(SRCS "logger_service.c"
    INCLUDE_DIRS "include"
    REQUIRES blackbox_logger telemetry_pipeline runtime_tasks event_bus metrics debug_pins)
