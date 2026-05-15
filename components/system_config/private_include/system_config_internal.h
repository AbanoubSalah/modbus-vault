/**
 * @file system_config_internal.h
 * @ingroup controller_module
 * @author Abanoub Salah
 *
 * @brief System configurations internal
 *
 * @details Contains System configurations internal structure(s)
 */

#ifndef SYSTEM_CONFIG_INTERNAL_H
#define SYSTEM_CONFIG_INTERNAL_H

#include "system_config.h"

/**
 * @brief System configuration structure
 */
typedef struct {
    rs485_driver_config_t rs485_driver; /**< RS485 driver configuration instance */
    modbus_analyzer_config_t analyzer;  /**< Modbus analyzer configuration instance */
    mqtt_bridge_config_t mqtt;          /**< MQTT bridge configuration instance */
    wifi_config_t wifi;                 /**< WiFi configuration instance */
    nvs_manager_ops_t nvs_ops;          /**< NVS operations instance */
    blackbox_logger_config_t logger;    /**< Blackbox logger configuration instance */
} system_config_t;

#endif
