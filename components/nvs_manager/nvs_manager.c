/**
 * @file nvs_manager.c
 * @ingroup nvs_manager_module
 * @brief Implementation of the NVS manager
 *
 * @details
 * - Abstract driver saving/loading by providing local application config structure
 * and saving/loading it as a blob to NVS
 * - Calculates and checks CRC16-Modbus on Saving/Loading
 * - Writes happens by writing to local application config structure
 * - On write marks application config structure as dirty to signal a potential NVS update
 * - Reads happens by reading from local application config structure
 * - Uses syncing timer to flush data periodically
 */

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs_manager_internal.h"
#include "utils.h"

#include <string.h>

/** NVS Manager TAG name */
static const char *TAG = "NVS_MANAGER";
/** Helps in write/read management */
static const key_map_t config_map[] = {
    {NVS_MANAGER_KEYS_WIFI_SSID, offsetof (app_config_t, wifi_ssid), NVS_MANAGER_MAX_WIFI_SSID_SIZE},
    {NVS_MANAGER_KEYS_WIFI_PASS, offsetof (app_config_t, wifi_password), NVS_MANAGER_MAX_WIFI_PASSWORD_SIZE},
    {NVS_MANAGER_KEYS_MQTT_URI, offsetof (app_config_t, mqtt_uri), NVS_MANAGER_MAX_MQTT_URI_SIZE},
    {NVS_MANAGER_KEYS_MQTT_USER, offsetof (app_config_t, mqtt_user), NVS_MANAGER_MAX_MQTT_USER_SIZE},
    {NVS_MANAGER_KEYS_MQTT_PASS, offsetof (app_config_t, mqtt_password), NVS_MANAGER_MAX_MQTT_PASSWORD_SIZE},
    {NVS_MANAGER_KEYS_BB_WRITE_OFFSET, offsetof (app_config_t, write_offset), sizeof (uint32_t)},
    {NVS_MANAGER_KEYS_BB_REPLAY_OFFSET, offsetof (app_config_t, replay_offset), sizeof (uint32_t)},
    {NVS_MANAGER_KEYS_BB_LAST_ID, offsetof (app_config_t, last_id), sizeof (uint32_t)}};
/** Defaulting value when no data in NVS */
static app_config_t default_config = {.version = NVS_MANAGER_CONFIG_VERSION};
/** App configuration lock */
static SemaphoreHandle_t config_lock = NULL;

/**
 * @brief Find entry in config map
 *
 * @param key Key chosen from nvs_keys_t
 * @return key_map_t* Pointer to map entry
 */
static const key_map_t *find_map_helper (nvs_keys_t key)
{
    const key_map_t *res = NULL;
    for (size_t idx = 0; idx < (sizeof (config_map) / sizeof (config_map[0])); ++idx)
    {
        if (config_map[idx].key == key)
        {
            res = &config_map[idx];
            break;
        }
    }

    return res;
}

/**
 * @brief Save configuration to flash
 *
 * @details Save configuration structure to flash as a blob
 * and update shadow structure if save succeeded
 *
 * @return esp_err_t Save result
 * @retval ESP_OK: Save success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval any Any error from down layers propagates upward
 */
static esp_err_t save_config_internal (void)
{
    esp_err_t err      = ESP_OK;
    bool is_lock_taken = false;

    if ((nvs_instance.handle == 0) || (nvs_instance.ops_ptr == NULL) ||
        (nvs_instance.ops_ptr->write == NULL) || (nvs_instance.ops_ptr->commit == NULL) ||
        (config_lock == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK)
    {
        if (xSemaphoreTake (config_lock, NVS_MANAGER_CONFIG_LOCK_TIMEOUT_MS) != pdTRUE)
        {
            err = ESP_ERR_TIMEOUT;
        }
        else
        {
            is_lock_taken = true;
        }
    }

    if (err == ESP_OK)
    {
        // Calculate CRC excluding CRC field itself
        nvs_instance.app_config.version = NVS_MANAGER_CONFIG_VERSION;
        nvs_instance.app_config.crc16 =
            calculate_modbus_crc16 ((uint8_t *) &nvs_instance.app_config, offsetof (app_config_t, crc16));

        // Write the whole struct as a BLOB
        err = nvs_instance.ops_ptr->write (nvs_instance.handle, NVS_MANAGER_CONFIG_KEY,
                                           &nvs_instance.app_config, sizeof (nvs_instance.app_config));
    }

    if (err == ESP_OK)
    {
        err = nvs_instance.ops_ptr->commit (nvs_instance.handle);
    }

    if (err == ESP_OK)
    {
        clear_dirty_flag_helper ();
        nvs_instance.shadow_app_config = nvs_instance.app_config;
    }

    if (is_lock_taken == true)
    {
        xSemaphoreGive (config_lock);
    }

    return err;
}

/**
 * @brief Load configuration from flash
 *
 * @details Load configuration from flash. On success compare version
 * and verify it's crc16 if all checks update shadow
 * structure then return
 *
 * @return esp_err_t Write result
 * @retval ESP_OK: Write success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval any Any error from down layers propagates upward
 *
 * @note On-fail it loads shadow configuration to primary configuration
 *       to undo any corruption happened during load attempt
 */
static esp_err_t load_config_internal (void)
{
    esp_err_t err      = ESP_OK;
    bool is_lock_taken = false;
    if ((nvs_instance.handle == 0) || (nvs_instance.ops_ptr == NULL) ||
        (nvs_instance.ops_ptr->read == NULL) || (config_lock == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK)
    {
        if (xSemaphoreTake (config_lock, NVS_MANAGER_CONFIG_LOCK_TIMEOUT_MS) != pdTRUE)
        {
            err = ESP_ERR_TIMEOUT;
        }
        else
        {
            is_lock_taken = true;
        }
    }

    if (err == ESP_OK)
    {
        size_t required_size = sizeof (nvs_instance.app_config);

        clear_dirty_flag_helper ();
        err = nvs_instance.ops_ptr->read (nvs_instance.handle, NVS_MANAGER_CONFIG_KEY,
                                          &nvs_instance.app_config, &required_size);
    }

    if (err == ESP_OK)
    {
        uint32_t expected_crc =
            calculate_modbus_crc16 ((uint8_t *) &nvs_instance.app_config, offsetof (app_config_t, crc16));

        if (nvs_instance.app_config.version != NVS_MANAGER_CONFIG_VERSION)
        {
            err = ESP_ERR_INVALID_VERSION;
        }

        if ((err == ESP_OK) && (nvs_instance.app_config.crc16 != expected_crc))
        {
            err = ESP_ERR_INVALID_CRC;
        }
    }

    if (err == ESP_OK)
    {
        nvs_instance.shadow_app_config = nvs_instance.app_config;
    }
    else
    {
        // Load fail -> restore old known config
        nvs_instance.app_config = nvs_instance.shadow_app_config;
    }

    if (is_lock_taken == true)
    {
        (void) xSemaphoreGive (config_lock);
    }

    return err;
}

/**
 * @brief Callback for timer
 *
 * @details Callback function for periodic timer. It checks for
 * dirty configuration then commit it if so
 *
 * @param arg_void_ptr Void pointer to callback argument
 */
static void check_dirty_commit_callback (void *arg_void_ptr)
{
    (void) arg_void_ptr;

    if (get_dirty_flag_helper () == true)
    {
        bool is_struct_dirty = false;
        if (config_lock != NULL)
        {
            if (xSemaphoreTake (config_lock, pdMS_TO_TICKS (NVS_MANAGER_CONFIG_LOCK_TIMEOUT_MS)) == pdTRUE)
            {
                // Check if structure changed
                is_struct_dirty = (memcmp (&nvs_instance.app_config, &nvs_instance.shadow_app_config,
                                           sizeof (nvs_instance.app_config)) != 0);
                xSemaphoreGive (config_lock);
            }
        }

        if (is_struct_dirty == true)
        {
            (void) save_config_internal ();
        }
        clear_dirty_flag_helper ();
    }
}

/**
 * @brief Write to configuration structure using an NVS key
 *
 * @param config_ptr Pointer to configuration structure
 * @param key Key chosen from nvs_keys_t
 * @param value_void_ptr Void pointer to value
 * @param value_size Value size
 *
 * @return esp_err_t write result
 * @retval ESP_OK Write success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 */
static esp_err_t write_to_cfg_struct_helper (app_config_t *config_ptr,
                                             nvs_keys_t key,
                                             const void *value_void_ptr,
                                             size_t value_size)
{
    esp_err_t err          = ESP_OK;
    const key_map_t *entry = find_map_helper (key);
    bool is_lock_taken     = false;

    if ((config_ptr == NULL) || (value_void_ptr == NULL) || (entry == NULL) || (value_size > entry->size) ||
        (config_lock == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    else
    {
        err = ((xSemaphoreTake (config_lock, pdMS_TO_TICKS (NVS_MANAGER_CONFIG_LOCK_TIMEOUT_MS)) != pdTRUE)
                   ? ESP_ERR_TIMEOUT
                   : ESP_OK);
        if (err == ESP_OK)
        {
            is_lock_taken = true;
        }
    }

    if (err == ESP_OK)
    {
        size_t write_length = ((value_size < entry->size) ? value_size : entry->size);
        (void) memcpy ((uint8_t *) (((uint8_t *) config_ptr) + entry->offset), value_void_ptr, write_length);
        if (write_length < entry->size)
        {
            (void) memset ((uint8_t *) (((uint8_t *) config_ptr) + entry->offset + write_length), 0x00,
                           (entry->size - write_length));
        }
    }

    if (is_lock_taken == true)
    {
        xSemaphoreGive (config_lock);
    }

    return err;
}

esp_err_t nvs_manager_init (nvs_manager_ops_t *ops_ptr)
{
    esp_err_t err = ESP_OK;
    if ((ops_ptr == NULL) || (ops_ptr->init == NULL) || (ops_ptr->erase == NULL) || (ops_ptr->open == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK)
    {
        config_lock = xSemaphoreCreateMutex ();
        err         = ((config_lock != NULL) ? ESP_OK : ESP_ERR_NO_MEM);
    }

    if (err == ESP_OK)
    {
        nvs_instance.ops_ptr = ops_ptr;
        err                  = ops_ptr->init ();
        // Setting default values
        // Note: if manager get initialized, defaults get replaced
        //       else deinit will reset it to zeros
        default_config.crc16 =
            calculate_modbus_crc16 ((uint8_t *) &default_config, offsetof (app_config_t, crc16));
        // Only shadow config, because, during 'config load' the primary config may get written over
        // and will get restored from shadow in 'config load' function
        nvs_instance.shadow_app_config = default_config;
    }

    if (err == ESP_OK)
    {
        // Get a handle to use it later for save/load
        err = ops_ptr->open (NVS_MANAGER_CONFIG_NAMESPACE, NVS_READWRITE, &nvs_instance.handle);
    }

    if (err == ESP_OK)
    {
        err = load_config_internal ();
        if (err != ESP_OK)
        {
            // Note: defaults already loaded a couple of lines ago
            ESP_LOGW (TAG, "Configurations load fail, reverting to default values");
            err = ESP_OK;
        }
    }

    if ((err == ESP_ERR_NVS_NO_FREE_PAGES) || (err == ESP_ERR_NVS_NEW_VERSION_FOUND))
    {
        ESP_LOGW (TAG, "Configuration was not found, resetting NVS");
        err = ops_ptr->erase ();
        if (err == ESP_OK)
        {
            err = ops_ptr->init ();
        }

        if (err != ESP_OK)
        {
            ESP_LOGW (TAG, "Reset fail");
        }
        else
        {
            ESP_LOGI (TAG, "Flash reset success, continuing with default values");
        }
    }

    if (err == ESP_OK)
    {
        // Setup a timer for saving app configuration
        const esp_timer_create_args_t timer_args = {.callback = check_dirty_commit_callback,
                                                    .name     = "nvs_sync_timer"};
        err = esp_timer_create (&timer_args, &nvs_instance.sync_timer_handle);
        if (err == ESP_OK)
        {
            // Start periodic sync
            err =
                esp_timer_start_periodic (nvs_instance.sync_timer_handle, NVS_MANAGER_WRITE_CYCLE_PERIOD_US);
        }
    }

    if (err == ESP_OK)
    {
        ESP_LOGI (TAG, "Initialized");
    }
    else
    {
        nvs_manager_deinit ();
        ESP_LOGW (TAG, "Failed to initialize: (%s).", esp_err_to_name (err));
    }

    return err;
}

esp_err_t nvs_manager_set_default (nvs_keys_t key, const void *value_void_ptr, size_t value_size)
{
    esp_err_t err          = ESP_OK;
    const key_map_t *entry = find_map_helper (key);

    if ((value_void_ptr == NULL) || (entry == NULL) || (value_size > entry->size))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    else if (config_lock != NULL)
    {
        err = ESP_ERR_INVALID_STATE;
    }
    else
    {
    }

    if (err == ESP_OK)
    {
        size_t write_length = ((value_size < entry->size) ? value_size : entry->size);
        (void) memcpy ((uint8_t *) (((uint8_t *) &default_config) + entry->offset), value_void_ptr,
                       write_length);
        if (write_length < entry->size)
        {
            (void) memset ((uint8_t *) (((uint8_t *) &default_config) + entry->offset + write_length), 0x00,
                           (entry->size - write_length));
        }
    }

    return err;
}

esp_err_t nvs_manager_write_cfg (nvs_keys_t key, const void *value_void_ptr, size_t value_size)
{
    esp_err_t err = write_to_cfg_struct_helper (&(nvs_instance.app_config), key, value_void_ptr, value_size);

    if (err == ESP_OK)
    {
        set_dirty_flag_helper ();
    }

    return err;
}

esp_err_t nvs_manager_read_cfg (nvs_keys_t key, void *value_void_ptr)
{
    esp_err_t err          = ESP_OK;
    const key_map_t *entry = find_map_helper (key);
    bool is_lock_taken     = false;

    if ((entry == NULL) || (value_void_ptr == NULL) || (config_lock == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK)
    {
        if (xSemaphoreTake (config_lock, pdMS_TO_TICKS (NVS_MANAGER_CONFIG_LOCK_TIMEOUT_MS)) != pdTRUE)
        {
            err = ESP_ERR_TIMEOUT;
        }
        else
        {
            is_lock_taken = true;
        }
    }

    if (err == ESP_OK)
    {
        (void) memcpy (value_void_ptr, (void *) (((uint8_t *) &(nvs_instance.app_config)) + entry->offset),
                       entry->size);
    }

    if (is_lock_taken == true)
    {
        xSemaphoreGive (config_lock);
    }

    return err;
}

esp_err_t nvs_manager_flush_cfg (void)
{
    return save_config_internal ();
}

void nvs_manager_deinit (void)
{
    if (nvs_instance.sync_timer_handle != NULL)
    {
        // Delete timer
        (void) esp_timer_delete (nvs_instance.sync_timer_handle);
    }

    // Commit any changes
    (void) save_config_internal ();

    if (nvs_instance.ops_ptr != NULL)
    {
        if ((nvs_instance.handle != 0) && (nvs_instance.ops_ptr->close != NULL))
        {
            // Close opened handle
            (void) nvs_instance.ops_ptr->close (nvs_instance.handle);
        }

        if (nvs_instance.ops_ptr->deinit != NULL)
        {
            // Deinit flash
            (void) nvs_instance.ops_ptr->deinit ();
        }
    }

    if (config_lock != NULL)
    {
        vSemaphoreDelete (config_lock);
        config_lock = NULL;
    }

    (void) memset (&nvs_instance, 0x00, sizeof (nvs_instance));
}
