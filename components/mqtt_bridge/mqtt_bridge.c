/**
 * @file mqtt_bridge.c
 * @ingroup telemetry_module
 * @brief Implementation of the mqtt bridge
 *
 * @details
 * - Initiate ESP MQTT using provided configuration
 * - Facilitate while connected
 * - Calls registered callback on MQTT connect/disconnect event
 */

#include "mqtt_bridge.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "metrics.h"

/** MQTT Bridge TAG name */
static const char *TAG = "MQTT_BRIDGE";

/**
 * @brief MQTT event handler
 *
 * @details Handles MQTT connect/disconnect events
 *
 * @param args_void_ptr Void pointer to handler arguments
 * @param base ESP event base
 * @param event_id Event id
 * @param event_data_void_ptr Void pointer to event data
 */
static void
mqtt_event_handler (void *args_void_ptr, esp_event_base_t base, int32_t event_id, void *event_data_void_ptr)
{
    (void) base;
    (void) event_id;

    const esp_mqtt_event_handle_t event  = (esp_mqtt_event_handle_t) event_data_void_ptr;
    mqtt_bridge_t *const mqtt_bridge_ptr = (mqtt_bridge_t *) args_void_ptr;

    if ((mqtt_bridge_ptr != NULL) && (mqtt_bridge_ptr->connection_lock != NULL) && (event != NULL))
    {
        switch (event->event_id)
        {
        case MQTT_EVENT_CONNECTED:
            if (xSemaphoreTake (mqtt_bridge_ptr->connection_lock, portMAX_DELAY) == pdTRUE)
            {
                mqtt_bridge_ptr->mqtt_connected = true;
                (void) xSemaphoreGive (mqtt_bridge_ptr->connection_lock);
                if (mqtt_bridge_ptr->notify_cb != NULL)
                {
                    mqtt_bridge_ptr->notify_cb (true);
                }
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            if (xSemaphoreTake (mqtt_bridge_ptr->connection_lock, portMAX_DELAY) == pdTRUE)
            {
                mqtt_bridge_ptr->mqtt_connected = false;
                (void) xSemaphoreGive (mqtt_bridge_ptr->connection_lock);
                if (mqtt_bridge_ptr->notify_cb != NULL)
                {
                    mqtt_bridge_ptr->notify_cb (false);
                }
            }
            break;
        default:
            break;
        }
    }
}

esp_err_t mqtt_bridge_init (mqtt_bridge_t *mqtt_bridge_ptr, const mqtt_bridge_config_t *config_ptr)
{
    esp_err_t err = ESP_OK;
    if ((mqtt_bridge_ptr == NULL) || (config_ptr == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK)
    {
        mqtt_bridge_ptr->connection_lock = xSemaphoreCreateMutex ();
        if (mqtt_bridge_ptr->connection_lock == NULL)
        {
            err = ESP_ERR_NO_MEM;
        }
    }

    if (err == ESP_OK)
    {
        // Initiate MQTT client
        mqtt_bridge_ptr->client = esp_mqtt_client_init (&config_ptr->client_config);
        if (mqtt_bridge_ptr->client == NULL)
        {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK)
    {
        // Register callback for MQTT events
        err = esp_mqtt_client_register_event (mqtt_bridge_ptr->client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                              mqtt_bridge_ptr);
    }

    if (err == ESP_OK)
    {
        // Start MQTT client
        err = esp_mqtt_client_start (mqtt_bridge_ptr->client);
    }

    if (err == ESP_OK)
    {
        mqtt_bridge_ptr->mqtt_connected = false;
        mqtt_bridge_ptr->notify_cb      = config_ptr->notify_cb;
        ESP_LOGI (TAG, "Initialized");
    }
    else
    {
        ESP_LOGW (TAG, "Failed to initialize: (%s).", esp_err_to_name (err));
        mqtt_bridge_deinit (mqtt_bridge_ptr);
    }

    return err;
}

esp_err_t mqtt_bridge_publish (const mqtt_bridge_t *mqtt_bridge_ptr,
                               const char *topic_ptr,
                               const char *data_ptr,
                               size_t length,
                               int32_t qos)
{
    esp_err_t err      = ESP_OK;
    bool is_lock_taken = false;

    if ((mqtt_bridge_ptr == NULL) || (mqtt_bridge_ptr->client == NULL) ||
        (mqtt_bridge_ptr->connection_lock == NULL) || (data_ptr == NULL) || (length == 0U))
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK)
    {
        err = ((mqtt_bridge_is_connected (mqtt_bridge_ptr) == true) ? ESP_OK : ESP_FAIL);
    }

    if ((err == ESP_OK))
    {
        BaseType_t sem_err = xSemaphoreTake (mqtt_bridge_ptr->connection_lock, portMAX_DELAY);
        if (sem_err == pdTRUE)
        {
            is_lock_taken = true;
            err           = ESP_OK;
        }
        else
        {
            err = ESP_ERR_TIMEOUT;
        }
    }

    if (err == ESP_OK)
    {
        int32_t msg_id = esp_mqtt_client_publish (mqtt_bridge_ptr->client, topic_ptr, (const char *) data_ptr,
                                                  length, qos, 0);
        err            = ((msg_id >= 0) ? ESP_OK : ESP_FAIL);
    }

    if (is_lock_taken == true)
    {
        (void) xSemaphoreGive (mqtt_bridge_ptr->connection_lock);
    }

    return err;
}

bool mqtt_bridge_is_connected (const mqtt_bridge_t *const mqtt_bridge_ptr)
{
    bool is_connected = false;

    if ((mqtt_bridge_ptr != NULL) && (mqtt_bridge_ptr->connection_lock != NULL))
    {
        xSemaphoreTake (mqtt_bridge_ptr->connection_lock, portMAX_DELAY);
        is_connected = mqtt_bridge_ptr->mqtt_connected;
        xSemaphoreGive (mqtt_bridge_ptr->connection_lock);
    }

    return is_connected;
}

void mqtt_bridge_deinit (mqtt_bridge_t *mqtt_bridge_ptr)
{
    if (mqtt_bridge_ptr != NULL)
    {
        if (mqtt_bridge_ptr->client != NULL)
        {
            // Stop, unregister event and destroy client
            (void) esp_mqtt_client_stop (mqtt_bridge_ptr->client);
            (void) esp_mqtt_client_unregister_event (mqtt_bridge_ptr->client, ESP_EVENT_ANY_ID,
                                                     mqtt_event_handler);
            (void) esp_mqtt_client_destroy (mqtt_bridge_ptr->client);
            mqtt_bridge_ptr->client = NULL;
        }

        if (mqtt_bridge_ptr->connection_lock != NULL)
        {
            // Free-up used memory resources
            vSemaphoreDelete (mqtt_bridge_ptr->connection_lock);
            mqtt_bridge_ptr->connection_lock = NULL;
        }
    }
}
