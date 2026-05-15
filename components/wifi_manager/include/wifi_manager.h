/**
 * @file wifi_manager.h
 * @ingroup wifi_manager_module
 * @author Abanoub Salah
 * @brief Wi-Fi connection manager with exponential backoff
 *
 * @details
 * - Features
 *     - Automatic reconnect with exponential backoff
 *     - Event-driven state transitions
 *     - Integration with system metrics
 *     - Thread-safe and non-blocking
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "esp_err.h"
#include "esp_wifi.h"

/**
 * @brief WiFi state enum
 */
typedef enum {
    WIFI_STATE_IDLE,        /**< Idle */
    WIFI_STATE_CONNECTING,  /**< Connecting */
    WIFI_STATE_CONNECTED,   /**< Connected and have an IP */
    WIFI_STATE_DISCONNECTED /**< Disconnected */
} wifi_state_t;

/**
 * @brief Initialize Wi-Fi manager
 *
 * @details Initialize different components needed by Wi-Fi
 * and register events to manage Wi-Fi connection state
 * and setup a one-shot timer to trigger on disconnect
 * event
 *
 * @param wifi_config_ptr Pointer to Wi-Fi configuration structure
 * @return esp_err_t
 * @retval ESP_OK Initialize success
 * @retval BLACKBOX_FAIL Initialize fail
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_NO_MEM No available memory for resource allocation
 * @retval any Any error from down layers propagates upward
 */
esp_err_t wifi_manager_init (wifi_config_t *wifi_config_ptr);

/**
 * @brief Get Wi-Fi current connection state
 *
 * @return wifi_state_t Current connection state
 */
wifi_state_t wifi_manager_get_state (void);

/**
 * @brief Trigger Wi-Fi re-connect
 *
 * @details Trigger Wi-Fi re-connect using one-shot timer
 * with an exponential back-off increment to avoid network congestion
 */
void wifi_manager_trigger_reconnect (void);

/**
 * @brief De-Initialize Wi-Fi manager
 *
 * @details Stops and deletes timer and stops Wi-Fi
 * then deinitialize Wi-Fi unregister events
 */
void wifi_manager_deinit (void);

#endif
