/**
 * @file mqtt_bridge.h
 * @ingroup telemetry_module
 * @author Abanoub Salah
 * @brief MQTT abstraction layer
 *
 * @details
 * - Features:
 *     - Thread-safe publish interface
 *     - Connection state tracking
 *     - Topic abstraction
 * - Designed to decouple application logic from ESP-MQTT.
 */

#ifndef MQTT_BRIDGE_H
#define MQTT_BRIDGE_H

#include "esp_err.h"
#include "mqtt_client.h"

typedef struct mqtt_bridge_t mqtt_bridge_t;     /**< MQTT Bridge declaration */
typedef void (*mqtt_bridge_notify_cb_t) (bool); /**< State change notify callback typedef */

/**
 * @brief MQTT bridge configuration structure
 */
typedef struct {
    esp_mqtt_client_config_t client_config; /**< MQTT client configuration instance */
    const char *const device_id;            /**< Publishing device ID */
    mqtt_bridge_notify_cb_t notify_cb;      /**< Callback on connectivity state change */
} mqtt_bridge_config_t;

/**
 * @brief MQTT bridge type structure
 */
struct mqtt_bridge_t {
    esp_mqtt_client_handle_t client;   /**< MQTT client configuration handle */
    SemaphoreHandle_t connection_lock; /**< Connection lock */
    mqtt_bridge_notify_cb_t notify_cb; /**< Callback on connectivity state change */
    bool mqtt_connected;               /**< MQTT connection state connected/disconnected */
};

/**
 * @brief Initialize MQTT bridge
 *
 * @details Initialize MQTT bridge by initiating MQTT client,
 * register to events and start client
 *
 * @param mqtt_bridge_ptr Pointer to MQTT bridge instance
 * @param config_ptr Pointer to MQTT bridge configuration instance
 *
 * @return esp_err_t Initialize result
 * @retval ESP_OK Initialize success
 * @retval ESP_FAIL Initialize fail
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_NO_MEM No available memory for resource allocation
 */
esp_err_t mqtt_bridge_init (mqtt_bridge_t *mqtt_bridge_ptr, const mqtt_bridge_config_t *config_ptr);

/**
 * @brief Publish to MQTT topic
 *
 * @param mqtt_bridge_ptr Pointer to MQTT bridge instance
 * @param topic_ptr Pointer to topic
 * @param data_ptr Pointer to data to be published
 * @param length Length of data
 * @param qos Message QoS
 *
 * @return esp_err_t Publish result
 * @retval ESP_OK Publish success
 * @retval ESP_FAIL Publish fail
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_TIMEOUT Timed out waiting for resource
 */
esp_err_t mqtt_bridge_publish (const mqtt_bridge_t *mqtt_bridge_ptr,
                               const char *topic_ptr,
                               const char *data_ptr,
                               size_t length,
                               int32_t qos);

/**
 * @brief Get MQTT connection status
 *
 * @param mqtt_bridge_ptr Pointer to MQTT bridge instance
 * @return bool true if connected false otherwise
 *
 * @note If mqtt_bridge_ptr is not valid 'false' is returned
 */
bool mqtt_bridge_is_connected (const mqtt_bridge_t *mqtt_bridge_ptr);

/**
 * @brief De-init MQTT bridge
 *
 * @param mqtt_bridge_ptr Pointer to MQTT bridge instance
 */
void mqtt_bridge_deinit (mqtt_bridge_t *mqtt_bridge_ptr);

#endif
