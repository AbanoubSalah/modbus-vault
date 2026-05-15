/**
 * @file nvs_manager.h
 * @ingroup nvs_manager_module
 * @author Abanoub Salah
 * @brief NVS abstraction layer
 *
 * @details
 * - Designed to decouple application logic from ESP-NVS by abstracting function calls
 * - Data integrity by CRC16-Modbus checking when saving/loading data
 */

#ifndef NVS_MANAGER_H
#define NVS_MANAGER_H

#include "esp_err.h"
#include "nvs_flash.h"

/** NVS key size of MQTT URI */
#define NVS_MANAGER_MAX_MQTT_URI_SIZE (128)
/** NVS key size of MQTT user */
#define NVS_MANAGER_MAX_MQTT_USER_SIZE (64)
/** NVS key size of MQTT password */
#define NVS_MANAGER_MAX_MQTT_PASSWORD_SIZE (64)
/** NVS key size of WiFi SSID */
#define NVS_MANAGER_MAX_WIFI_SSID_SIZE (32)
/** NVS key size of WiFi password */
#define NVS_MANAGER_MAX_WIFI_PASSWORD_SIZE (64)

/**
 * @brief NVS manager keys enum
 */
typedef enum {
    NVS_MANAGER_KEYS_WIFI_SSID        = 0, /**< WiFi SSID */
    NVS_MANAGER_KEYS_WIFI_PASS        = 1, /**< WiFi password */
    NVS_MANAGER_KEYS_MQTT_URI         = 2, /**< MQTT URI */
    NVS_MANAGER_KEYS_MQTT_USER        = 3, /**< MQTT user */
    NVS_MANAGER_KEYS_MQTT_PASS        = 4, /**< MQTT password */
    NVS_MANAGER_KEYS_BB_WRITE_OFFSET  = 5, /**< Blackbox Logger write offset */
    NVS_MANAGER_KEYS_BB_REPLAY_OFFSET = 6, /**< Blackbox Logger replay offset */
    NVS_MANAGER_KEYS_BB_LAST_ID       = 7, /**< Blackbox Logger last ID */
    NVS_MANAGER_KEYS_MAX                   /**< Keys count */
} nvs_keys_t;

/**
 * @brief NVS manager driver hooks
 */
typedef struct {
    // Driver hooks
    esp_err_t (*init) (void);                                              /**< init function */
    esp_err_t (*open) (const char *, nvs_open_mode_t, nvs_handle_t *);     /**< open function */
    esp_err_t (*read) (nvs_handle_t, const char *, void *, size_t *);      /**< read function */
    esp_err_t (*write) (nvs_handle_t, const char *, const void *, size_t); /**< write function */
    esp_err_t (*erase) (void);                                             /**< erase function */
    esp_err_t (*commit) (nvs_handle_t);                                    /**< commit function */
    void (*close) (nvs_handle_t);                                          /**< close function */
    esp_err_t (*deinit) (void);                                            /**< deinit function */
} nvs_manager_ops_t;

/**
 * @brief Initialize NVS manager
 *
 * @details Initialize NVS manager by calling flash
 * init, if succeeded load config, If failed with
 * 'flash full' or 'new NVS version' erase flash then
 * try to call flash init again. Finally initiate
 * periodic timer to commit configuration if it was
 * flagged dirty and newer than shadow structure
 *
 * @param ops_ptr Pointer to NVS manager operations structure
 *
 * @return esp_err_t Initialize result
 * @retval ESP_OK Initialize success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval any Any error from down layers propagates upward
 *
 * @note Assumes default name for NVS partition typically 'nvs'
 *
 * @note WARNING: If there are no free-pages or NVS-full
 *                NVS partition gets erased
 */
esp_err_t nvs_manager_init (nvs_manager_ops_t *ops_ptr);

/**
 * @brief Set configuration default
 *
 * @details Set configuration default values in case it was missing from
 * flash
 *
 * @param key Key chosen from nvs_keys_t
 * @param value_void_ptr Pointer to data
 * @param value_size Value size
 *
 * @return esp_err_t Set result
 * @retval ESP_OK Set success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 *
 * @note Defaults has no effect after initialization it should be used before
 * initialization
 */
esp_err_t nvs_manager_set_default (nvs_keys_t key, const void *value_void_ptr, size_t value_size);

/**
 * @brief Write configuration to flash
 *
 * @details Write configuration to flash by writing to configuration
 * structure and flush it when preset time has passed
 *
 * @param key Key chosen from nvs_keys_t
 * @param value_void_ptr Pointer to data
 * @param value_size Value size
 *
 * @return esp_err_t Write result
 * @retval ESP_OK Write success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 */
esp_err_t nvs_manager_write_cfg (nvs_keys_t key, const void *value_void_ptr, size_t value_size);

/**
 * @brief Read configuration from flash
 *
 * @details Read configuration from flash cached in configuration
 * structure
 *
 * @param key Key chosen from nvs_keys_t
 * @param value_void_ptr Pointer to data
 *
 * @return esp_err_t Read result
 * @retval ESP_OK Read success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 */
esp_err_t nvs_manager_read_cfg (nvs_keys_t key, void *value_void_ptr);

/**
 * @brief Flush configuration structure to flash
 *
 * @return esp_err_t Flush result
 * @retval ESP_OK Flush success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval any Any error from down layers propagates upward
 */
esp_err_t nvs_manager_flush_cfg (void);

/**
 * @brief Deinitialize NVS manager
 *
 * @details Deinitialize NVS manager by commit data, close handle,
 * delete timer and deinit flash
 */
void nvs_manager_deinit (void);

#endif
