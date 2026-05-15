/**
 * @file wifi_manager.c
 * @ingroup wifi_manager_module
 * @brief Implementation of the wifi manager
 *
 * @details
 * - Initializes WiFi with provided configurations as in station mode
 * - Registers to events to manage WiFi connection state with a lock for mutual access
 * - Creates one-shot timer to reconnect with an exponential backoff timing
 */

#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"

#include <string.h>

#define WIFI_MANAGE_MAXIMUM_RECONNECT_TIMEOUT_US                                                             \
    (60000000UL) /**< Maximum reconnect timeout in micro seconds */
#define WIFI_MANAGE_MINIMUM_RECONNECT_TIMEOUT_US                                                             \
    (1000000) /**< Minimum reconnect timeout in micro seconds                                                \
               */

/** WiFi Manager TAG name */
static const char *TAG = "WIFI_MANAGER";
/**  WiFi Manager state lock */
static SemaphoreHandle_t state_lock = NULL;
/**  WiFi Manager state */
static wifi_state_t wifi_manager_state = WIFI_STATE_IDLE;
/**  WiFi Manager reconnect timer handle */
static esp_timer_handle_t reconnect_timer = NULL;
/**  WiFi Manager reconnect current timeout */
static uint64_t reconnect_timeout_us = WIFI_MANAGE_MINIMUM_RECONNECT_TIMEOUT_US;

/** WiFi event context instance */
static esp_event_handler_instance_t wifi_event_ctx = NULL;
/** WiFi event handler instance */
static esp_event_handler_instance_t ip_event_ctx = NULL;

/**
 * @brief Set current WiFi state
 *
 * @param state WiFi state
 */
static void wifi_manager_set_state_helper (const wifi_state_t state)
{
    if (state_lock != NULL)
    {
        xSemaphoreTake (state_lock, portMAX_DELAY);
        wifi_manager_state = state;
        xSemaphoreGive (state_lock);
    }
}

/**
 * @brief WiFi event handler
 *
 * @details
 * - Handles
 *     - WIFI_EVENT_STA_START
 *     - WIFI_EVENT_STA_DISCONNECTED
 *     - IP_EVENT_STA_GOT_IP
 *
 * @param args_void_ptr Void pointer to handler arguments
 * @param event_base ESP event base
 * @param event_id Event id
 * @param event_data_void_ptr Void pointer to event data
 */
static void wifi_event_handler (void *args_void_ptr,
                                esp_event_base_t event_base,
                                int32_t event_id,
                                void *event_data_void_ptr)
{
    (void) args_void_ptr;
    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            wifi_manager_set_state_helper (WIFI_STATE_CONNECTING);
            (void) esp_wifi_connect ();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            wifi_manager_set_state_helper (WIFI_STATE_DISCONNECTED);
            ESP_LOGI (TAG, "Disconnected. Retrying connection...");
            wifi_manager_trigger_reconnect ();
            break;
        default:
            break;
        }
    }

    if ((event_base == IP_EVENT) && (event_id == IP_EVENT_STA_GOT_IP))
    {
        wifi_manager_set_state_helper (WIFI_STATE_CONNECTED);
        reconnect_timeout_us = WIFI_MANAGE_MINIMUM_RECONNECT_TIMEOUT_US;
        ESP_LOGI (TAG, "Got IP: " IPSTR, IP2STR (&((ip_event_got_ip_t *) event_data_void_ptr)->ip_info.ip));
    }
}

/**
 * @brief Callback to reconnect on one-shot timer
 *
 * @param arg_void_ptr Void pointer to callback argument
 */
static void reconnect_timer_callback (void *arg_void_ptr)
{
    (void) arg_void_ptr;
    wifi_state_t current_state = wifi_manager_get_state ();

    if (current_state != WIFI_STATE_CONNECTED)
    {
        (void) esp_wifi_connect ();
    }

    // Check the current state and re-arm the reconnect timer if we are still not connected
    current_state = wifi_manager_get_state ();
    if ((current_state == WIFI_STATE_DISCONNECTED) || (current_state == WIFI_STATE_CONNECTING))
    {
        wifi_manager_trigger_reconnect ();
    }
}

esp_err_t wifi_manager_init (wifi_config_t *wifi_config_ptr)
{
    esp_err_t err = ESP_OK;

    if (wifi_config_ptr == NULL)
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK)
    {
        state_lock = xSemaphoreCreateMutex ();
        if (state_lock == NULL)
        {
            err = ESP_ERR_NO_MEM;
        }
    }

    // Basic Stack Init
    if (err == ESP_OK)
    {
        err = esp_netif_init ();
    }

    if (err == ESP_OK)
    {
        err = esp_event_loop_create_default ();
    }

    if (err == ESP_OK)
    {
        if (esp_netif_create_default_wifi_sta () == NULL)
        {
            err = ESP_FAIL;
        }
    }

    // Wi-Fi Driver Init
    if (err == ESP_OK)
    {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT ();
        err                    = esp_wifi_init (&cfg);
    }

    // Register Handlers
    if (err == ESP_OK)
    {
        err = esp_event_handler_instance_register (WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
                                                   &wifi_event_ctx);
    }

    if (err == ESP_OK)
    {
        err = esp_event_handler_instance_register (IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
                                                   &ip_event_ctx);
    }

    // Configuration
    if (err == ESP_OK)
    {
        err = esp_wifi_set_storage (WIFI_STORAGE_RAM);
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_set_mode (WIFI_MODE_STA);
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_set_config (WIFI_IF_STA, wifi_config_ptr);
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_start ();
    }

    // Timer Creation
    if (err == ESP_OK)
    {
        const esp_timer_create_args_t timer_args = {.callback = &reconnect_timer_callback,
                                                    .name     = "wifi_reconnect_timer"};
        err                                      = esp_timer_create (&timer_args, &reconnect_timer);
    }

    // Final check
    if (err != ESP_OK)
    {
        ESP_LOGW (TAG, "Failed to initialize: (%s).", esp_err_to_name (err));
        wifi_manager_deinit ();
    }
    else
    {
        ESP_LOGI (TAG, "Initialized");
    }

    return err;
}

wifi_state_t wifi_manager_get_state (void)
{
    wifi_state_t state = WIFI_STATE_IDLE;
    if (state_lock != NULL)
    {
        xSemaphoreTake (state_lock, portMAX_DELAY);
        state = wifi_manager_state;
        xSemaphoreGive (state_lock);
    }

    return state;
}

void wifi_manager_trigger_reconnect (void)
{
    if (reconnect_timer != NULL)
    {
        reconnect_timeout_us = (((reconnect_timeout_us << 1) < WIFI_MANAGE_MAXIMUM_RECONNECT_TIMEOUT_US)
                                    ? (reconnect_timeout_us << 1)
                                    : WIFI_MANAGE_MAXIMUM_RECONNECT_TIMEOUT_US);

        // Ensure clean state
        (void) esp_timer_stop (reconnect_timer);
        (void) esp_timer_start_once (reconnect_timer, reconnect_timeout_us);
    }
}

void wifi_manager_deinit (void)
{
    // Delete Timer
    if (reconnect_timer != NULL)
    {
        (void) esp_timer_stop (reconnect_timer);
        (void) esp_timer_delete (reconnect_timer);
        reconnect_timer = NULL;
    }

    // Stop Wi-Fi first
    if (esp_wifi_stop () == ESP_OK)
    {
        // Then deinit
        (void) esp_wifi_deinit ();
    }

    // Unregister event handlers
    if (ip_event_ctx != NULL)
    {
        (void) esp_event_handler_instance_unregister (IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_ctx);
        ip_event_ctx = NULL;
    }

    if (wifi_event_ctx != NULL)
    {
        (void) esp_event_handler_instance_unregister (WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_ctx);
        wifi_event_ctx = NULL;
    }

    // Delete mutex
    if (state_lock != NULL)
    {
        vSemaphoreDelete (state_lock);
        state_lock = NULL;
    }
}
