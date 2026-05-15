idf_component_register(SRCS "telemetry_pipeline.c"
    INCLUDE_DIRS "include"
    REQUIRES slab_pool serializer debug_pins)
