idf_component_register(SRCS "controller.c" "system_init.c" "router.c"
    INCLUDE_DIRS "include"
    REQUIRES system_config esp_wifi metrics slab_pool wifi_manager nvs_manager event_bus runtime_tasks logger_service telemetry_pipeline telemetry_service utils)
