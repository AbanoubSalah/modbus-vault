/**
 * @file nvs_manager_internal.h
 * @ingroup nvs_manager_module
 * @author Abanoub Salah
 * @brief NVS abstraction layer internal
 */

#ifndef NVS_MANAGER_INTERNAL_H
#define NVS_MANAGER_INTERNAL_H

#include "nvs_manager.h"

/** NVS Manager version */
#define NVS_MANAGER_CONFIG_VERSION (1U)
/** Storage namespace */
#define NVS_MANAGER_CONFIG_NAMESPACE ("storage")
/** Stored configuration key */
#define NVS_MANAGER_CONFIG_KEY ("app_cfg")
/** Timer trigger period in micro seconds */
#define NVS_MANAGER_WRITE_CYCLE_PERIOD_US (30000000UL)
/** Configuration lock wait timeout */
#define NVS_MANAGER_CONFIG_LOCK_TIMEOUT_MS (500U)

/**
 * @brief NVS manager application config structure
 */
typedef struct {
    uint32_t version;                                       /**< Configuration version */
    char wifi_ssid[NVS_MANAGER_MAX_WIFI_SSID_SIZE];         /**< WiFi SSID */
    char wifi_password[NVS_MANAGER_MAX_WIFI_PASSWORD_SIZE]; /**< WiFi password */
    char mqtt_uri[NVS_MANAGER_MAX_MQTT_URI_SIZE];           /**< MQTT URI */
    char mqtt_user[NVS_MANAGER_MAX_MQTT_USER_SIZE];         /**< MQTT user */
    char mqtt_password[NVS_MANAGER_MAX_MQTT_PASSWORD_SIZE]; /**< MQTT password */
    uint32_t write_offset;                                  /**< Blackbox Logger write offset */
    uint32_t replay_offset;                                 /**< Blackbox Logger replay offset */
    uint32_t last_id;                                       /**< Blackbox Logger last ID */
    uint16_t crc16; /**< CRC16 for this entire structure excluding crc16 itself */
} app_config_t;
_Static_assert (sizeof (app_config_t) % 4 == 0, "app_config_t size must be a multiple of 4");

/**
 * @brief NVS manager keys map structure
 */
typedef struct {
    nvs_keys_t key; /**< NVS key */
    size_t offset;  /**< offset from containing structure */
    size_t size;    /**< Key size in structure */
} key_map_t;

/**
 * @brief NVS manager type structure
 */
typedef struct {
    nvs_manager_ops_t *ops_ptr;           /**< Pointer to manager operations structure */
    app_config_t app_config;              /**< Application configuration structure */
    app_config_t shadow_app_config;       /**< Shadow application configuration structure */
    nvs_handle_t handle;                  /**< Open NVS handle */
    esp_timer_handle_t sync_timer_handle; /**< Syncing timer handle */
    bool is_dirty;                        /**< Application structure dirty flag */
} nvs_manager_t;

/** The Only instance of the structure */
static nvs_manager_t nvs_instance = {0};

/**
 * @brief Set configuration dirty flag
 */
static inline void set_dirty_flag_helper (void)
{
    nvs_instance.is_dirty = true;
}

/**
 * @brief Get configuration dirty flag
 *
 * @return bool true on dirty configuration false otherwise
 */
static inline bool get_dirty_flag_helper (void)
{
    return nvs_instance.is_dirty;
}

/**
 * @brief Clear configuration dirty flag
 */
static inline void clear_dirty_flag_helper (void)
{
    nvs_instance.is_dirty = false;
}

#endif
