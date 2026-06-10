set(component_srcs "system_config.c")

# Only compile the provisioning file(s) if the user enabled it
if(CONFIG_WIFI_MODE_USE_BLE_PROVISIONING)
    list(APPEND component_srcs "ble_provisioner.c")
endif()

idf_component_register(
    SRCS ${component_srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_wifi rs485_driver modbus_analyzer nvs_manager blackbox_logger mqtt_bridge wifi_manager network_provisioning utils
)