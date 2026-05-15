idf_component_register(SRCS "nvs_manager.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES nvs_flash utils)
