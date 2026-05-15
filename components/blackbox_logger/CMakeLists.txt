idf_component_register(SRCS "blackbox_logger.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_timer esp_partition utils)
