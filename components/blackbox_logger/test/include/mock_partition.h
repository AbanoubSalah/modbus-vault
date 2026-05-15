#ifndef MOCK_PARTITION_H
#define MOCK_PARTITION_H

#include "esp_err.h"
#include "esp_partition.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define MOCK_FLASH_SIZE  (16 * 1024) // 16KB
#define MOCK_SECTOR_SIZE (4096)

/**
 * @brief Mock partition
 */
typedef struct {
    uint8_t memory[MOCK_FLASH_SIZE]; /**< Memory */
    size_t size;                     /**< Memory size  */

    // Fault injection
    bool fail_next_write;    /**< Fail next write */
    size_t fail_after_bytes; /**< Fail after byte */
} mock_partition_t;

/**
 * @brief Read from flash
 *
 * @param partition_ptr Pointer to partition
 * @param offset Read offset
 * @param data_void_ptr Pointer to data buffer
 * @param size Data size to be read
 *
 * @return esp_err_t read result
 * @retval ESP_OK: Read success
 * @retval ESP_ERR_INVALID_SIZE Offset exceeded partition size
 *
 */
esp_err_t mock_read (const esp_partition_t *partition_ptr, size_t offset, void *data_void_ptr, size_t size);

/**
 * @brief Write to flash
 *
 * @param partition_ptr Pointer to partition
 * @param offset Write offset
 * @param data_void_ptr Pointer to data buffer
 * @param size Data size to be wrote
 *
 * @return esp_err_t Write result
 * @retval ESP_OK: Write success
 * @retval ESP_FAIL Write fail as requested
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_INVALID_SIZE Offset exceeded partition size
 * @retval ESP_ERR_INVALID_STATE Enforcing 'Flash Rule'
 *
 * @note Write enforces "Physical NOR Flash Rule: bits can only transition from 1 to 0"
 */
esp_err_t
mock_write (const esp_partition_t *partition_ptr, size_t offset, const void *data_void_ptr, size_t size);

/**
 * @brief Erase flash
 *
 * @param partition_ptr Pointer to partition
 * @param offset Erase start offset
 * @param size Erase size
 *
 * @return esp_err_t Erase result
 * @retval ESP_OK: Erase success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_INVALID_SIZE Offset exceeded partition size
 *
 * @note offset and size must be aligned with sector size
 */
esp_err_t mock_erase_range (const esp_partition_t *partition_ptr, size_t offset, size_t size);

/**
 * @brief Resets flash
 */
void mock_flash_reset (void);

/**
 * @brief Inject write failure
 *
 * @param after_bytes Fail at/after byte
 */
void mock_inject_write_failure (size_t after_bytes);

#endif
