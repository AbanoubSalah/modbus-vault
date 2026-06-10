/**
 * @file system_config.h
 * @ingroup controller_module
 * @author Abanoub Salah
 *
 * @brief Contains different system-wide configurations
 */

#ifndef SYSTEM_CONFIG_H
#define SYSTEM_CONFIG_H

#include "blackbox_logger.h"
#include "esp_err.h"
#include "esp_partition.h"
#include "esp_wifi.h"
#include "modbus_analyzer.h"
#include "mqtt_bridge.h"
#include "mqtt_client.h"
#include "nvs_manager.h"
#include "rs485_driver.h"
#include "sdkconfig.h"

#define EVENT_BUS_QUEUE_SIZE (50U) /**< Event queue size */

#define RS485_DRIVER_RX_BUFFER_SIZE (2048U) /**< UART receive buffer size */
#define RS485_DRIVER_RX_QUEUE_SIZE  (20U)   /**< UART receive queue size */

#define SYSTEM_CONFIG_WIFI_SSID     (CONFIG_WIFI_SSID)     /**< WiFi SSID */
#define SYSTEM_CONFIG_WIFI_PASSWORD (CONFIG_WIFI_PASSWORD) /**< WiFi password */
#define SYSTEM_CONFIG_MQTT_URI      (CONFIG_MQTT_URI)      /**< MQTT URI */
#define SYSTEM_CONFIG_MQTT_USER     (CONFIG_MQTT_USER)     /**< MQTT user */
#define SYSTEM_CONFIG_MQTT_PASSWORD (CONFIG_MQTT_PASSWORD) /**< MQTT password */

#define SYSTEM_CONFIG_NVS_PARTITION_NAME       ("nvs")         /**< NVS partitions name */
#define SYSTEM_CONFIG_LOGGER_PARTITION_NAME    ("log_storage") /**< Logger partitions name */
#define SYSTEM_CONFIG_LOGGER_PARTITION_SUBTYPE (0x40)          /**< Logger partitions subtype */
#define SYSTEM_CONFIG_CERTS_PARTITION_LABEL    ("certs")       /**< Certificates partitions name */
#define SYSTEM_CONFIG_CERTS_PARTITION_SUBTYPE  (0x41)          /**< Certificates partitions subtype */
#define SYSTEM_CONFIG_MTLS_HEADER_MAGIC                                                                      \
    (0x43455254UL) /**< Magic header for certificates partition 'CERT'                                       \
                    */

#define BLACKBOX_LOGGER_DISK_FLUSH_TIMEOUT_US                                                                \
    (CONFIG_BLACKBOX_LOGGER_FLUSH_TIMEOUT) /**< Logger flush timeout in micro seconds */
#define BLACKBOX_LOGGER_SECTOR_SIZE (CONFIG_BLACKBOX_LOGGER_SECTOR_SIZE) /**< Logger flash sector size */
#define BLACKBOX_LOGGER_BATCH_SIZE  (CONFIG_BLACKBOX_LOGGER_BATCH_SIZE)  /**< Logger batch buffer size */
#define BLACKBOX_LOGGER_ALIGN       (CONFIG_BLACKBOX_LOGGER_ALIGNMENT) /**< Logger flash address alignment */

#define SYSTEM_CONFIG_SAVE_PERIOD_US                                                                         \
    (CONFIG_SYSTEM_CONFIG_SAVE_PERIOD) /**< System configuration saving period in micro seconds */

/**
 * @brief Setup default configurations
 *
 * @return esp_err_t Set result
 * @retval ESP_OK Set success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 *
 * Must be called before NVS initialization
 */
esp_err_t system_config_setup_defaults (void);

/**
 * @brief Setup system configurations
 *
 * @details
 * - Configure components using preset values or loaded from NVS
 * before system initialization
 * - Setup a periodic timer to save updated parameters
 *
 * @return esp_err_t Setup result
 * @retval ESP_OK Setup success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_INVALID_STATE Configuration in invalid state
 * @retval ESP_ERR_NO_MEM No available memory for resource allocation
 */
esp_err_t system_config_setup (void);

/**
 * @brief Get rs485 driver configuration
 *
 * @return rs485_driver_config_t*
 */
rs485_driver_config_t *system_config_get_rs485_driver_config (void);

/**
 * @brief Get Modbus analyzer configuration
 *
 * @return modbus_analyzer_config_t*
 */
modbus_analyzer_config_t *system_config_get_modbus_analyzer_config (void);

/**
 * @brief Get Blackbox logger configuration
 *
 * @return blackbox_logger_config_t*
 */
blackbox_logger_config_t *system_config_get_blackbox_logger_config (void);

/**
 * @brief Get MQTT bridge configuration
 *
 * @return mqtt_bridge_config_t*
 */
mqtt_bridge_config_t *system_config_get_mqtt_config (void);

/**
 * @brief Get WiFi configuration
 *
 * @return wifi_config_t*
 */
wifi_config_t *system_config_get_wifi_config (void);

/**
 * @brief Get NVS operations
 *
 * @return nvs_manager_ops_t*
 */
nvs_manager_ops_t *system_config_get_nvs_manager_ops (void);

/**
 * @brief Reset system config
 *
 */
void system_config_reset (void);

#endif
