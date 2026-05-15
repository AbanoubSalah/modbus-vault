#ifndef MOCK_NVS_H
#define MOCK_NVS_H

#include "esp_err.h"
#include "nvs.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MOCK_FLASH_SIZE            (16 * 1024) // 16KB
#define MOCK_NAMESPACE_MAX_SIZE    (32U)
#define MOCK_KEY_MAX_SIZE          (32U)
#define MOCK_HANDLE                (1)
#define MOCK_HANDLE_NAMESPACE_NAME ("storage")
#define MOCK_CONFIG_KEY            ("app_cfg")

/**
 * @brief Mock NVS type structure
 */
typedef struct {
    uint8_t memory[MOCK_FLASH_SIZE];              /**< Memory buffer */
    size_t size;                                  /**< Memory buffer size */
    size_t write_size;                            /**< Last written size */
    char namespace_name[MOCK_NAMESPACE_MAX_SIZE]; /**< Stores namespace name */
    char key[MOCK_KEY_MAX_SIZE];                  /**< Stores last used key name */
    nvs_handle_t handle;                          /**< NVS handle provided by open */
} mock_nvs_t;

static mock_nvs_t nvs_mock = {0};

/**
 * @brief Initialize RAM
 *
 * @details Initializes mock structure with preset namespace
 * name and clears memory buffer also sets initial values for
 * same structure
 *
 * @return esp_err_t
 * @retval ESP_OK Initialize success
 */
esp_err_t ram_init (void)
{
    (void) snprintf (nvs_mock.namespace_name, MOCK_NAMESPACE_MAX_SIZE, "%s", MOCK_HANDLE_NAMESPACE_NAME);
    (void) snprintf (nvs_mock.key, MOCK_KEY_MAX_SIZE, "%s", MOCK_CONFIG_KEY);
    nvs_mock.size   = MOCK_FLASH_SIZE;
    nvs_mock.handle = 0;

    return ESP_OK;
}

/**
 * @brief open namespace
 *
 * @details Open namespace and provide handle on success
 *
 * @param namespace_name Namespace name
 * @param mode Mode of operation
 * @param handle_ptr Pointer to handle
 *
 * @return esp_err_t Open result
 * @retval ESP_OK Open success
 * @retval ESP_ERR_NO_MEM Namespace or handle not available
 *
 * @note Operation mode not implemented
 *
 */
esp_err_t ram_open (const char *namespace_name, nvs_open_mode_t mode, nvs_handle_t *handle_ptr)
{
    esp_err_t err = ESP_OK;

    (void) mode;

    if ((strcmp (namespace_name, nvs_mock.namespace_name) == 0) && (nvs_mock.handle == 0))
    {
        *handle_ptr     = MOCK_HANDLE;
        nvs_mock.handle = MOCK_HANDLE;
    }
    else
    {
        err = ESP_ERR_NO_MEM;
    }

    return err;
}

/**
 * @brief Read from RAM
 *
 * @param handle Open provided handle
 * @param key_ptr Pointer to read key
 * @param dst Read buffer
 * @param size_ptr Pointer to read size
 *
 * @return esp_err_t Read result
 * @retval ESP_OK Read success
 * @retval ESP_FAIL Read fail
 */
esp_err_t ram_read (nvs_handle_t handle, const char *key_ptr, void *dst, size_t *size_ptr)
{
    esp_err_t err = ESP_OK;
    if ((strcmp (key_ptr, nvs_mock.key) == 0) && (handle == nvs_mock.handle) &&
        (*size_ptr <= MOCK_FLASH_SIZE))
    {
        (void) memcpy (dst, nvs_mock.memory, *size_ptr);
    }
    else
    {
        err = ESP_FAIL;
    }

    return err;
}

/**
 * @brief Write to RAM
 *
 * @param handle Open provided handle
 * @param key_ptr Pointer to write key
 * @param src Write buffer
 * @param size Write size
 *
 * @return esp_err_t Write result
 * @retval ESP_OK Write success
 * @retval ESP_FAIL Write fail
 */
esp_err_t ram_write (nvs_handle_t handle, const char *key_ptr, const void *src, size_t size)
{
    esp_err_t err = ESP_OK;

    if ((handle == nvs_mock.handle) && (size <= nvs_mock.size))
    {
        (void) memcpy (nvs_mock.memory, src, size);
    }
    else
    {
        err = ESP_FAIL;
    }

    return err;
}

/**
 * @brief Erase RAM
 *
 * @return esp_err_t Erase result
 * @retval ESP_OK Erase success
 */
esp_err_t ram_erase (void)
{
    memset (nvs_mock.memory, 0x00, MOCK_FLASH_SIZE);

    return ESP_OK;
}

/**
 * @brief Commit to RAM
 *
 * @param handle Open provided handle
 *
 * @return esp_err_t Commit result
 * @retval ESP_OK Commit success
 * @retval ESP_FAIL Commit fail
 */
esp_err_t ram_commit (nvs_handle_t handle)
{
    esp_err_t err = ESP_OK;

    if (handle != nvs_mock.handle)
    {
        err = ESP_FAIL;
    }

    return err;
}

/**
 * @brief Close RAM
 *
 * @param handle Open provided handle
 */
void ram_close (nvs_handle_t handle)
{
    if (handle == nvs_mock.handle)
    {
        nvs_mock.handle = 0;
    }
}

/**
 * @brief Deinit RAM
 *
 * @return esp_err_t Deinit result
 * @retval ESP_OK Deinit success
 */
esp_err_t ram_deinit (void)
{
    nvs_mock.handle = 0;
    return ESP_OK;
}

#endif
