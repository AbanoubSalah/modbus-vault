/**
 * @file blackbox_logger.h
 * @ingroup logger_module
 * @author Abanoub Salah
 * @brief Flash-backed circular logging system
 *
 * @details
 * - Features
 *     - Power-loss resilient logging
 *     - CRC-protected entries
 *     - Circular buffer with overwrite protection
 *     - Replay support for deferred transmission
 *     - Designed for high-reliability embedded systems
 */

#ifndef BLACKBOX_LOGGER_H
#define BLACKBOX_LOGGER_H

#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdint.h>

/**
 * @brief BlackBox Logger status codes enum
 */
typedef enum {
    BLACKBOX_LOGGER_OK,                /**< Successful operation */
    BLACKBOX_LOGGER_ERR_FAIL,          /**< Failed operation */
    BLACKBOX_LOGGER_ERR_INVALID_ARG,   /**< Invalid arguments provided */
    BLACKBOX_LOGGER_ERR_NO_MEM,        /**< Insufficient memory available */
    BLACKBOX_LOGGER_ERR_TIMEOUT,       /**< The operation timed out */
    BLACKBOX_LOGGER_ERR_INVALID_CRC,   /**< Invalid CRC */
    BLACKBOX_LOGGER_ERR_INVALID_ENTRY, /**< Invalid Logger entry */
    BLACKBOX_LOGGER_ERR_ERASE_FAIL,    /**< Failed to erase flash */
    BLACKBOX_LOGGER_ERR_WRITE_FAIL,    /**< Failed to write to flash */
    BLACKBOX_LOGGER_ERR_PROCESS_FAIL,  /**< Failed to process replay entry */
} blackbox_logger_err_t;

/**
 * @brief Blackbox logger entry view structure
 */
typedef struct {
    uint8_t *data_ptr; /**< Pointer to data */
    size_t length;     /**< Length of data */
} blackbox_logger_entry_view_t;

typedef bool (*blackbox_logger_iter_cb_t) (
    void *, const blackbox_logger_entry_view_t *); /**< Iterator callback for replay definition */

/**
 * @brief BlackBox logger configuration parameters structure
 */
typedef struct {
    uint32_t write_offset;  /**< Address of the start of the last successful write */
    uint32_t replay_offset; /**< Last known replay address */
    uint32_t last_id;       /**< The ID of the last known entry */
    bool is_dirty;          /**< Structure is dirty flag should be set when written to */
} blackbox_logger_parameters_t;

/**
 * @brief BlackBox logger configuration structure
 */
typedef struct {
    esp_partition_t *partition_ptr;          /**< Point to logging partition */
    blackbox_logger_parameters_t parameters; /**< Parameters instance saved as recover hint */

    const uint8_t address_align;           /**< Address alignment imposed by system */
    const uint64_t flush_timer_timeout_us; /**< One-shot timer timeout for flushing */

    // Pointers/Sizes to user-allocated buffers
    uint8_t *const sector_buf_ptr; /**< Pointer to sector buffer */
    const size_t sector_buf_size;  /**< Sector buffer size */
    uint8_t *const batch_buf_ptr;  /**< Pointer to batch buffer */
    const size_t batch_buf_size;   /**< Batch buffer size */

    void (*on_replay_available_func) (void); /**< Callback for when replay available */

    // Driver hooks
    esp_err_t (*write_func) (const esp_partition_t *,
                             size_t,
                             const void *const,
                             size_t); /**< Write to flash function */
    esp_err_t (*read_func) (const esp_partition_t *,
                            size_t,
                            void *const,
                            size_t);                                   /**< Read from flash function */
    esp_err_t (*erase_func) (const esp_partition_t *, size_t, size_t); /**< Erase flash function */
} blackbox_logger_config_t;

/**
 * @brief BlackBox logger type structure
 */
typedef struct {
    // Configuration
    blackbox_logger_config_t *config_ptr; /**< Pointer to Logger configurations */

    // Logger states
    uint32_t write_offset;      /**< Write offset for last committed entry */
    uint32_t replay_offset;     /**< Replay offset for current entry */
    uint32_t last_id;           /**< Last used ID */
    int32_t last_erased_sector; /**< Last erased sector. '-1' for none */
    size_t batch_len;           /**< Length of current data in buffer */

    // Flags and OS handles
    SemaphoreHandle_t sector_buf_lock; /**< Handle for sector lock */
    SemaphoreHandle_t write_lock;      /**< Handle for write lock */
    esp_timer_handle_t flush_timer;    /**< One-shot timer re-triggered on write to buffer to flush after a
                                          preset-time of inactivity */
} blackbox_logger_t;

/**
 * @brief Initialize BlackBox Logger
 *
 * @details Initialize logger with provided configurations using parameters
 * as hints for recover also setup a one-shot timer with a preset-time for
 * flushing buffered data to flash
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 * @param config_ptr Pointer to BlackBox Logger configuration instance
 *
 * @return blackbox_logger_err_t Initialize result
 * @retval BLACKBOX_LOGGER_OK Initialize success
 * @retval BLACKBOX_FAIL Initialize Fail
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_NO_MEM No available memory for resource allocation
 */
blackbox_logger_err_t blackbox_logger_init (blackbox_logger_t *logger_ptr,
                                            blackbox_logger_config_t *config_ptr);

/**
 * @brief Write an entry to flash
 *
 * @details Write to buffer first with a pre-defined maximum length
 * if exceeded, it gets flushed. Entries are wrapped between header
 * and tail with crc16 to ensures data integrity after power loss
 * Look at blackbox_logger_flush for more details
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 * @param entry_ptr Pointer to entry to be logged
 *
 * @return blackbox_logger_err_t Write result
 * @retval BLACKBOX_LOGGER_OK: Write success
 * @retval BLACKBOX_LOGGER_ERR_FAIL Failed to flush writing buffer
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_NO_MEM No available memory for resource allocation
 * @retval BLACKBOX_LOGGER_ERR_TIMEOUT Timed out waiting for resource
 *
 * @note Maximum entry length is set by 'batch size' in kconfig
 *       and must be less than or equal to 'sector size' and
 *       sized must be aligned to system imposed alignments
 */
blackbox_logger_err_t blackbox_logger_write (blackbox_logger_t *logger_ptr,
                                             const blackbox_logger_entry_view_t *entry_ptr);

/**
 * @brief Read an entry from flash
 *
 * @details Read to a buffer with a capacity provided by caller
 * from a given flash offset
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 * @param read_offset Offset to read from
 * @param entry_ptr Pointer to entry to be logged
 * @param entry_buf_max_capacity Provided buffer maximum capacity
 *
 * @return blackbox_logger_err_t read result
 * @retval BLACKBOX_LOGGER_OK: Read success
 * @retval BLACKBOX_LOGGER_ERR_FAIL Failed to read from partition
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_NO_MEM No available memory for entry data
 * @retval BLACKBOX_LOGGER_ERR_TIMEOUT Timed out waiting for resource
 * @retval BLACKBOX_LOGGER_ERR_INVALID_CRC Invalid CRC for the entry
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ENTRY Invalid entry found
 */
blackbox_logger_err_t blackbox_logger_read (const blackbox_logger_t *logger_ptr,
                                            uint32_t read_offset,
                                            blackbox_logger_entry_view_t *entry_ptr,
                                            size_t entry_buf_max_capacity);

/**
 * @brief Flush buffered data to flash
 *
 * @details
 * - Flush buffered data to flash taking into account wrap-around at flash-end
 * - Calls notify callback if set and replay available
 * - Log entry format is as follows
 *     -
 * [HEADER][PAYLOAD][TAIL][ALIGNMENT]...[HEADER][PAYLOAD][TAIL][ALIGNMENT]
 * - Where:
 *     - [HEADER]: Is a prefix to the payload with
 *                   - Header magic number
 *                   - Monotonically increasing ID
 *                   - Payload length
 *     - [PAYLOAD]: Entry data
 *     - [TAIL]: Is a postfix to the payload with
 *                   - CRC-16-Modbus for [HEADER][PAYLOAD]
 *                   - Tail magic number
 *     - [ALIGNMENT]: Stuffing to ensure next entry is aligned with system imposed alignment
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 *
 * @return blackbox_logger_err_t Flush result
 * @retval BLACKBOX_LOGGER_OK: Flush success
 * @retval BLACKBOX_LOGGER_ERR_FAIL Failed to write/erase partition
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_NO_MEM Flash is full
 * @retval BLACKBOX_LOGGER_ERR_TIMEOUT Timed out waiting for resource
 *
 * @note Writes won't extend to next sector to prevent entry fragmentation
 *       that means every sector beginning may have a start of an entry and
 *       will fail if entry is bigger than sector length
 *
 * @note Function does not write buffer as a whole chunk it tries to fill
 *       available sector space first if possible to save on space
 *
 * @note - Replay/Write offsets interaction behavior is configurable in kconfig
 *           - Keep old entries: Returns with 'no memory' error code
 *           - Replace old entries: Advance replay offset to the beginning of
 *             next write sector jumping-over any replay leftover in sector since
 *             they will get erased with the whole sector for coming fresh writes
 */
blackbox_logger_err_t blackbox_logger_flush (blackbox_logger_t *logger_ptr);

/**
 * @brief Replay next available log entry
 *
 * @details calling a callback function on next available log entry
 * then advance replay offset on callback returns true otherwise returns
 * with error code
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 * @param is_entry_processed_cb_func Callback for when entry is available
 * @param cb_arg_void_ptr Void pointer to callback argument
 * @param entry_ptr Pointer to entry to read to
 * @param entry_buf_max_capacity Provided buffer maximum capacity
 *
 * @return blackbox_logger_err_t Replay result
 * @retval BLACKBOX_LOGGER_OK: Replay success
 * @retval BLACKBOX_LOGGER_ERR_FAIL Failed to read from partition
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_NO_MEM No available memory for entry data
 * @retval BLACKBOX_LOGGER_ERR_TIMEOUT Timed out waiting for resource
 * @retval BLACKBOX_LOGGER_ERR_INVALID_CRC Invalid CRC for the entry
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ENTRY Invalid entry found
 * @retval BLACKBOX_LOGGER_ERR_PROCESS_FAIL Callback refused/failed to process entry
 *
 * @note Function assumes every entry header starts at an aligned memory address
 *
 * @note replay_offset gets incremented if callback function return true which
 *       means entry was processed
 *
 * @note - On-Corrupt entry recovery strategy
 *           - If entry validation fails, assume flash corruption or power-loss mid-write
 *           - Synchronize replay_offset to write_offset to prevent undefined behavior
 *           - This guarantees forward progress at the cost of losing corrupted entries
 */
blackbox_logger_err_t blackbox_logger_next_replay (blackbox_logger_t *logger_ptr,
                                                   blackbox_logger_iter_cb_t is_entry_processed_cb_func,
                                                   void *cb_arg_void_ptr,
                                                   blackbox_logger_entry_view_t *entry_ptr,
                                                   const size_t entry_buf_max_capacity);

/**
 * @brief Iterate over available log entries
 *
 * @details Iterate over available log entries calling a callback function
 * on success stopping-on empty log or when callback returns false
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 * @param is_entry_processed_cb_func Callback for when entry is available
 * @param cb_arg_void_ptr Void pointer to callback argument
 * @param entry_ptr Pointer to entry to read to
 * @param entry_buf_max_capacity Provided buffer maximum capacity
 *
 * @return blackbox_logger_err_t Replay result
 * @retval BLACKBOX_LOGGER_OK: Replay success
 * @retval BLACKBOX_LOGGER_ERR_FAIL Failed to read from partition
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_NO_MEM No available memory for entry data
 * @retval BLACKBOX_LOGGER_ERR_TIMEOUT Timed out waiting for resource
 * @retval BLACKBOX_LOGGER_ERR_INVALID_CRC Invalid CRC for the entry
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ENTRY Invalid entry found
 * @retval BLACKBOX_LOGGER_ERR_PROCESS_FAIL Callback refused/failed to process entry
 *
 * @note See blackbox_logger_next_replay notes since this function loop call it
 */
blackbox_logger_err_t blackbox_logger_iterate_replay (blackbox_logger_t *logger_ptr,
                                                      blackbox_logger_iter_cb_t is_entry_processed_cb_func,
                                                      void *cb_arg_void_ptr,
                                                      blackbox_logger_entry_view_t *entry_ptr,
                                                      const size_t entry_buf_max_capacity);

/**
 * @brief Get Logger parameters defaults
 *
 * @param parameter_ptr Pointer to parameters structure
 */
void blackbox_logger_get_parameters_defaults (blackbox_logger_parameters_t *parameter_ptr);

/**
 * @brief Is log data available for replay
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 *
 * @return true if data available, false otherwise
 */
bool blackbox_logger_has_replay_data (const blackbox_logger_t *logger_ptr);

/**
 * @brief De-init BlackBox Logger
 *
 * @details De-init BlackBox Logger by flushing then stopping and deleting
 * the timer and free-up used resources
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 */
void blackbox_logger_deinit (blackbox_logger_t *logger_ptr);

#endif
