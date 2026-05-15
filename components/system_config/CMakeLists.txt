idf_component_register(SRCS "system_config.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_wifi rs485_driver modbus_analyzer nvs_manager blackbox_logger mqtt_bridge wifi_manager)
