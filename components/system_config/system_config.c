/**
 * @file system_config.c
 * @ingroup controller_module
 * @author Abanoub Salah
 *
 * @brief Implementation of the system configurations
 *
 * @details
 * - Setup system configuration from
 *     - kconfig
 *     - NVS manager
 *     - Macro defines
 * - Glues different aspects of system to configuration to allow initialization
 * - Provides APIs to allow other system components to get their needed configuration
 * - Provides helper function(s) needed by the system to work
 *     - Save parameters of logger to NVS
 */
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "system_config_internal.h"
#include "utils.h"
#include "wifi_manager.h"

#ifdef CONFIG_WIFI_MODE_USE_BLE_PROVISIONING
#include "ble_provisioner.h"
#endif

#include <string.h>

/** System config TAG name */
static const char *TAG = "SYSTEM_CONFIG";

/** Expected sector buffer by Blackbox Logger */
static uint8_t sector_buf[BLACKBOX_LOGGER_SECTOR_SIZE];
/** Expected batch buffer by Blackbox Logger */
static uint8_t batch_buf[BLACKBOX_LOGGER_BATCH_SIZE];

/** Buffer to save MQTT URI */
static char system_config_mqtt_uri[NVS_MANAGER_MAX_MQTT_URI_SIZE];
/** Buffer to save MQTT user */
static char system_config_mqtt_user[NVS_MANAGER_MAX_MQTT_USER_SIZE];
/** Buffer to save MQTT password */
static char system_config_mqtt_pass[NVS_MANAGER_MAX_MQTT_PASSWORD_SIZE];

/** Timer handle for saving parameters */
static esp_timer_handle_t save_param_timer_handle;

/** System's configurations structure */
static system_config_t global_config = {
    .rs485_driver = {.baudrate       = CONFIG_RS484_DRIVER_UART_BAUDRATE,
                     .port           = CONFIG_RS484_DRIVER_UART_PORT,
                     .data_bits      = CONFIG_RS484_DRIVER_UART_DATA_BITS,
                     .parity         = CONFIG_RS484_DRIVER_UART_PARITY,
                     .rx_pin         = CONFIG_RS484_DRIVER_UART_RX_PIN,
                     .rx_buffer_size = RS485_DRIVER_RX_BUFFER_SIZE,
                     .queue_size     = RS485_DRIVER_RX_QUEUE_SIZE},
    .analyzer     = {.rs485_driver_config_ptr = NULL},
    .mqtt         = {.device_id = CONFIG_MQTT_DEVICE_ID},
    .nvs_ops      = {.init   = nvs_flash_init,
                     .open   = nvs_open,
                     .read   = nvs_get_blob,
                     .write  = nvs_set_blob,
                     .erase  = nvs_flash_erase,
                     .commit = nvs_commit,
                     .close  = nvs_close,
                     .deinit = nvs_flash_deinit},
    .logger       = {.sector_buf_ptr         = sector_buf,
                     .sector_buf_size        = sizeof (sector_buf),
                     .batch_buf_ptr          = batch_buf,
                     .batch_buf_size         = sizeof (batch_buf),
                     .address_align          = BLACKBOX_LOGGER_ALIGN,
                     .flush_timer_timeout_us = BLACKBOX_LOGGER_DISK_FLUSH_TIMEOUT_US,
                     .write_func             = esp_partition_write,
                     .read_func              = esp_partition_read,
                     .erase_func             = esp_partition_erase_range}};

#if CONFIG_MQTT_TRANSPORT_SECURE
/**
 * @brief MQTT certificates/key header structure
 */
typedef struct {
    uint32_t magic;      /**< Header magic */
    uint32_t cli_offset; /**< Client's certificate offset */
    uint32_t cli_length; /**< Client's certificate length */
    uint32_t key_offset; /**< Client's key offset */
    uint32_t key_length; /**< Client's key length */
    uint32_t ca_offset;  /**< CA's offset */
    uint32_t ca_length;  /**< CA's length */
    uint32_t padding;    /** Padding to 32 bytes */
} cert_header_t;
_Static_assert (sizeof (cert_header_t) == 32,
                "cert_header_t layout must be exactly 32 bytes for 'Flash Encryption' alignment");

/** MM tracking handle to unmap if needed */
static esp_partition_mmap_handle_t certs_map_handle = 0;

/**
 * @brief Load MQTT certificates from partition using 'memory map'
 *
 * @return esp_err_t Load result
 * @retval ESP_OK Load success
 * @retval ESP_ERR_NOT_FOUND Partition not found
 * @retval ESP_ERR_INVALID_STATE Possible double call or corrupt partition
 * @retval ESP_ERR_INVALID_CRC CRC mismatch
 * @retval Any other error from memory mapping API
 */
static esp_err_t system_config_load_mqtt_certs (void)
{
    esp_err_t err = ESP_OK;

    if (certs_map_handle != 0)
    {
        err = ESP_ERR_INVALID_STATE;
    }

    const esp_partition_t *partition_ptr = NULL;
    if (err == ESP_OK)
    {
        // Locate certs' custom partition by name
        partition_ptr =
            esp_partition_find_first (ESP_PARTITION_TYPE_DATA, SYSTEM_CONFIG_CERTS_PARTITION_SUBTYPE,
                                      SYSTEM_CONFIG_CERTS_PARTITION_LABEL);

        if (partition_ptr == NULL)
        {
            err = ESP_ERR_NOT_FOUND;
            ESP_LOGE (TAG, "Certificates partition not found!");
        }
    }

    // Map the partition into the CPU's space
    const void *map_ptr = NULL;
    if (err == ESP_OK)
    {
        err = esp_partition_mmap (partition_ptr, 0, partition_ptr->size, ESP_PARTITION_MMAP_DATA, &map_ptr,
                                  &certs_map_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE (TAG, "MMU failed: %s", esp_err_to_name (err));
        }
    }

    const cert_header_t *header_ptr = (const cert_header_t *) map_ptr;
    if (err == ESP_OK)
    {
        // Verify magic bytes
        if (header_ptr->magic != SYSTEM_CONFIG_MTLS_HEADER_MAGIC)
        {
            ESP_LOGE (TAG, "Invalid certificates header magic!");
            esp_partition_munmap (certs_map_handle);
            certs_map_handle = 0;
            err              = ESP_ERR_INVALID_STATE;
        }
    }

    if (err == ESP_OK)
    {
        // Calculate CRC offset
        uint32_t unpadded_payload_size = header_ptr->ca_offset + header_ptr->ca_length;
        uint32_t total_current_length  = unpadded_payload_size + sizeof (uint16_t);
        uint32_t padding_size          = (32 - (total_current_length % 32)) % 32;
        uint32_t crc_offset            = unpadded_payload_size + padding_size;

        // Read stored CRC
        uint16_t stored_crc;
        memcpy (&stored_crc, (const uint8_t *) map_ptr + crc_offset, sizeof (uint16_t));

        uint16_t calculated_crc = calculate_modbus_crc16 ((const uint8_t *) map_ptr, crc_offset);

        if (calculated_crc != stored_crc)
        {
            ESP_LOGE (TAG, "CRC verification failed! calculated: %04" PRIX16 ", Expected: %04" PRIX16,
                      calculated_crc, stored_crc);
            err = ESP_ERR_INVALID_CRC;
        }
    }

    if (err == ESP_OK)
    {
        // MQTT client configuration
        global_config.mqtt.client_config.credentials.authentication.certificate =
            (const char *) (map_ptr + header_ptr->cli_offset);
        global_config.mqtt.client_config.credentials.authentication.key =
            (const char *) (map_ptr + header_ptr->key_offset);
        global_config.mqtt.client_config.credentials.authentication.certificate_len = header_ptr->cli_length;
        global_config.mqtt.client_config.credentials.authentication.key_len         = header_ptr->key_length;

        // MQTT broker configuration
        global_config.mqtt.client_config.broker.verification.certificate =
            (const char *) (map_ptr + header_ptr->ca_offset);
        global_config.mqtt.client_config.broker.verification.certificate_len = header_ptr->ca_length;

        ESP_LOGI (TAG, "Loaded certificates successfully.");
    }

    return err;
}
#endif

/**
 * @brief Callback to save parameters to NVS on timer timeout
 *
 * @param arg_void_ptr Void pointer to argument
 */
static void save_parameters_callback (void *arg_void_ptr)
{
    (void) arg_void_ptr;
    if (global_config.logger.parameters.is_dirty == true)
    {
        // save current parameters to NVS
        nvs_manager_write_cfg (NVS_MANAGER_KEYS_BB_REPLAY_OFFSET,
                               &global_config.logger.parameters.replay_offset,
                               sizeof (global_config.logger.parameters.replay_offset));
        nvs_manager_write_cfg (NVS_MANAGER_KEYS_BB_WRITE_OFFSET,
                               &global_config.logger.parameters.write_offset,
                               sizeof (global_config.logger.parameters.write_offset));
        nvs_manager_write_cfg (NVS_MANAGER_KEYS_BB_LAST_ID, &global_config.logger.parameters.last_id,
                               sizeof (global_config.logger.parameters.last_id));
    }
}

esp_err_t system_config_setup_defaults (void)
{
    esp_err_t err = ESP_OK;
#if CONFIG_WIFI_MODE_USE_MANUAL_SETTING
    // Use compile-time configurations
    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_WIFI_SSID, SYSTEM_CONFIG_WIFI_SSID,
                                       sizeof (SYSTEM_CONFIG_WIFI_SSID));
    }

    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_WIFI_PASS, SYSTEM_CONFIG_WIFI_PASSWORD,
                                       sizeof (SYSTEM_CONFIG_WIFI_PASSWORD));
    }
#else
    // Set empty as sentinel value
    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_WIFI_SSID, "", 1);
    }

    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_WIFI_PASS, "", 1);
    }
#endif

    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_MQTT_URI, SYSTEM_CONFIG_MQTT_URI,
                                       sizeof (SYSTEM_CONFIG_MQTT_URI));
    }

    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_MQTT_USER, SYSTEM_CONFIG_MQTT_USER,
                                       sizeof (SYSTEM_CONFIG_MQTT_USER));
    }

    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_MQTT_PASS, SYSTEM_CONFIG_MQTT_PASSWORD,
                                       sizeof (SYSTEM_CONFIG_MQTT_PASSWORD));
    }

    blackbox_logger_parameters_t bb_parameters;
    blackbox_logger_get_parameters_defaults (&bb_parameters);
    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_BB_WRITE_OFFSET, &bb_parameters.write_offset,
                                       sizeof (bb_parameters.write_offset));
    }

    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_BB_REPLAY_OFFSET, &bb_parameters.replay_offset,
                                       sizeof (bb_parameters.replay_offset));
    }

    if (err == ESP_OK)
    {
        err = nvs_manager_set_default (NVS_MANAGER_KEYS_BB_LAST_ID, &bb_parameters.last_id,
                                       sizeof (bb_parameters.last_id));
    }

    return err;
}

esp_err_t system_config_setup (void)
{
    esp_err_t err = ESP_OK;

    // Modbus analyzer configuration
    global_config.analyzer.rs485_driver_config_ptr = &(global_config.rs485_driver);

    // Setup Logger configuration
    global_config.logger.partition_ptr = (esp_partition_t *) esp_partition_find_first (
        ESP_PARTITION_TYPE_DATA, SYSTEM_CONFIG_LOGGER_PARTITION_SUBTYPE, SYSTEM_CONFIG_LOGGER_PARTITION_NAME);

    // Load Logger Parameters
    (void) nvs_manager_read_cfg (NVS_MANAGER_KEYS_BB_WRITE_OFFSET,
                                 &global_config.logger.parameters.write_offset);
    (void) nvs_manager_read_cfg (NVS_MANAGER_KEYS_BB_REPLAY_OFFSET,
                                 &global_config.logger.parameters.replay_offset);
    (void) nvs_manager_read_cfg (NVS_MANAGER_KEYS_BB_LAST_ID, &global_config.logger.parameters.last_id);

    // Load WiFi Credentials
    global_config.wifi.sta.threshold.authmode = CONFIG_WIFI_AUTH_MODE;
    (void) nvs_manager_read_cfg (NVS_MANAGER_KEYS_WIFI_SSID, global_config.wifi.sta.ssid);
    (void) nvs_manager_read_cfg (NVS_MANAGER_KEYS_WIFI_PASS, global_config.wifi.sta.password);

    bool needs_provisioning = false;
#if CONFIG_WIFI_MODE_USE_PROVISIONING
    // Check if NVS is blank 'sentinel value'. If it is, provision
    if (strlen ((char *) global_config.wifi.sta.ssid) == 0)
    {
        needs_provisioning = true;
    }
#else
    // Else manual setting
    needs_provisioning = false;
#endif

    if (needs_provisioning == true)
    {
#if CONFIG_WIFI_MODE_USE_PROVISIONING
        ESP_LOGW (TAG, "Launching BLE provisioner...");

        // Start provisioner
        err = ble_provisioner_start ();
        if (err == ESP_OK)
        {

            // Halt execution, provisioner event loop will catch the incoming events,
            // commit the keys to NVS, and execute esp_restart().
            while (true)
            {
                vTaskDelay (pdMS_TO_TICKS (1000));
            }
        }
#endif
    }

#if CONFIG_MQTT_TRANSPORT_SECURE
    if (err == ESP_OK)
    {
        err = system_config_load_mqtt_certs ();
    }
#endif

    // Assign MQTT Configuration
    (void) nvs_manager_read_cfg (NVS_MANAGER_KEYS_MQTT_URI, system_config_mqtt_uri);
    (void) nvs_manager_read_cfg (NVS_MANAGER_KEYS_MQTT_USER, system_config_mqtt_user);
    (void) nvs_manager_read_cfg (NVS_MANAGER_KEYS_MQTT_PASS, system_config_mqtt_pass);
    global_config.mqtt.client_config.broker.address.uri                  = system_config_mqtt_uri;
    global_config.mqtt.client_config.credentials.username                = system_config_mqtt_user;
    global_config.mqtt.client_config.credentials.authentication.password = system_config_mqtt_pass;

    if (err == ESP_OK)
    {
        // Timer for saving parameters
        const esp_timer_create_args_t timer_args = {.callback = save_parameters_callback,
                                                    .name     = "save_param_timer"};
        err                                      = esp_timer_create (&timer_args, &save_param_timer_handle);
    }

    if (err == ESP_OK)
    {
        // Start periodic sync
        err = esp_timer_start_periodic (save_param_timer_handle, SYSTEM_CONFIG_SAVE_PERIOD_US);
    }

    if (err == ESP_OK)
    {
        ESP_LOGI (TAG, "Configuration complete");
    }
    else
    {
        ESP_LOGE ("DEBUG", "Configuration error: %s", esp_err_to_name (err));
    }

    return err;
}

mqtt_bridge_config_t *system_config_get_mqtt_config (void)
{
    return &(global_config.mqtt);
}

rs485_driver_config_t *system_config_get_rs485_driver_config (void)
{
    return &(global_config.rs485_driver);
}

modbus_analyzer_config_t *system_config_get_modbus_analyzer_config (void)
{
    return &(global_config.analyzer);
}

blackbox_logger_config_t *system_config_get_blackbox_logger_config (void)
{
    return &(global_config.logger);
}

wifi_config_t *system_config_get_wifi_config (void)
{
    return &(global_config.wifi);
}

nvs_manager_ops_t *system_config_get_nvs_manager_ops (void)
{
    return &(global_config.nvs_ops);
}

void system_config_reset (void)
{
    if (save_param_timer_handle != NULL)
    {
        (void) esp_timer_stop (save_param_timer_handle);
        (void) esp_timer_delete (save_param_timer_handle);
        save_param_timer_handle = NULL;
    }
#if CONFIG_MQTT_TRANSPORT_SECURE
    if (certs_map_handle != 0)
    {
        // Un-map mapped memory
        esp_partition_munmap (certs_map_handle);
        certs_map_handle = 0;
        // MQTT client configuration
        global_config.mqtt.client_config.credentials.authentication.certificate     = NULL;
        global_config.mqtt.client_config.credentials.authentication.key             = NULL;
        global_config.mqtt.client_config.credentials.authentication.certificate_len = 0;
        global_config.mqtt.client_config.credentials.authentication.key_len         = 0;

        // MQTT broker configuration
        global_config.mqtt.client_config.broker.verification.certificate     = NULL;
        global_config.mqtt.client_config.broker.verification.certificate_len = 0;
    }
#endif
}
