/**
 * @file blackbox_logger.c
 * @ingroup logger_module
 * @brief Implementation of the blackbox logger
 *
 * @details
 * - This module uses a circular buffer approach to ensure we only
 * perform block-erases when necessary, extending flash life
 * - Recovery done with linear search starting from beginning if
 * provided hint prove invalid
 * - Write done using local memory buffer 'batch buffer' before
 * flushing. flushing is forced under
 *       - Buffer is full or
 *       - Preset-Time passed since last buffer write
 * - Read done using a user-provided memory buffer
 */

#include "blackbox_logger_internal.h"
#include "esp_log.h"
#include "utils.h"

/** Blackbox Logger TAG name */
static const char *TAG = "BB_LOG";
/** Blackbox Logger default parameters structure */
static blackbox_logger_parameters_t blackbox_logger_default_parameters = {
    .write_offset = 0, .replay_offset = 0, .last_id = 0, .is_dirty = false};

/**
 * @brief Check if valid data entry
 *
 * @param data_ptr Pointer to data
 * @param data_size Size of data buffer
 *
 * @return blackbox_logger_err_t Is data entry result
 *         BLACKBOX_LOGGER_OK Valid data entry
 * @retval BLACKBOX_LOGGER_ERR_NO_MEM No available memory for entry data
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_INVALID_CRC Invalid CRC for the entry
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ENTRY Invalid entry
 */
static blackbox_logger_err_t is_data_entry_internal (const uint8_t *data_ptr, const size_t data_size)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;

    if (data_ptr == NULL)
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        blackbox_logger_entry_header_t entry_header;
        (void) memcpy (&entry_header, data_ptr, sizeof (entry_header));

        // Verify entry header
        if (entry_header.header_magic != BLACKBOX_LOGGER_HEADER_MAGIC)
        {
            err = BLACKBOX_LOGGER_ERR_INVALID_ENTRY;
        }

        if ((err == BLACKBOX_LOGGER_OK) && (data_size >= BLACKBOX_LOGGER_ENTRY_SIZE_WITHOUT_DATA_LENGTH))
        {
            // Calculate offset for tail and CRC verification
            const uint32_t tail_offset = sizeof (blackbox_logger_entry_header_t) + entry_header.length;
            blackbox_logger_entry_tail_t entry_tail;
            (void) memcpy (&entry_tail, &data_ptr[tail_offset], sizeof (entry_tail));

            // Check if total entry size fits
            if ((tail_offset + sizeof (blackbox_logger_entry_tail_t)) > data_size)
            {
                err = BLACKBOX_LOGGER_ERR_NO_MEM;
            }

            // Verify entry integrity
            const uint16_t expected_crc = calculate_modbus_crc16 (data_ptr, tail_offset);
            if ((err == BLACKBOX_LOGGER_OK) && (entry_tail.crc != expected_crc))
            {
                err = BLACKBOX_LOGGER_ERR_INVALID_CRC;
            }

            // Verify entry tail
            if ((err == BLACKBOX_LOGGER_OK) && (entry_tail.tail_magic != BLACKBOX_LOGGER_TAIL_MAGIC))
            {
                err = BLACKBOX_LOGGER_ERR_INVALID_ENTRY;
            }
        }
    }

    return err;
}

/**
 * @brief Validate entry
 *
 * @details Checks header, tail and CRC validity
 *
 * @param data_ptr Pointer to data
 * @param data_size Size of data
 *
 * @return blackbox_logger_err_t Validate entry result
 * @retval BLACKBOX_LOGGER_OK: Valid entry
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_INVALID_CRC Invalid CRC for the entry
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ENTRY Invalid entry
 */
static blackbox_logger_err_t validate_entry_internal (const uint8_t *data_ptr, const size_t data_size)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;

    if (data_ptr == NULL)
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (data_size < BLACKBOX_LOGGER_ENTRY_SIZE_WITHOUT_DATA_LENGTH)
    {
        // Size doesn't match an empty-entry
        err = BLACKBOX_LOGGER_ERR_INVALID_ENTRY;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        err = is_data_entry_internal (data_ptr, data_size);
    }

    return err;
}

/**
 * @brief Get maximum chunk size of entries with ceiling
 *
 * @details Get maximum bytes to take from buffer that contains entries
 * and doesn't exceed ceiling size
 *
 * @param buf_ptr Pointer to entries buffer
 * @param buf_size Buffer size
 * @param ceiling_size Size of maximum bytes to get as ceiling
 * @param align_to Align to nearest
 *
 * @return size_t Maximum bytes to take from buffer
 *
 * @note Function doesn't validate entries, it just reads headers
 */
size_t get_entries_chunk_size_with_ceiling_helper (const uint8_t *buf_ptr,
                                                   const size_t buf_size,
                                                   const size_t ceiling_size,
                                                   const size_t align_to)
{
    size_t max_size   = 0;
    bool is_chunk_fit = true;

    if (buf_ptr == NULL)
    {
        is_chunk_fit = false;
    }

    size_t buf_offset = 0;
    while ((is_chunk_fit == true) && (buf_offset < buf_size))
    {
        blackbox_logger_entry_header_t *entry_header_ptr =
            (blackbox_logger_entry_header_t *) &buf_ptr[buf_offset];

        if (entry_header_ptr->header_magic == BLACKBOX_LOGGER_HEADER_MAGIC)
        {
            uint32_t current_entry_size =
                entry_total_aligned_size_helper (entry_header_ptr->length, align_to);
            if ((max_size + current_entry_size) <= ceiling_size)
            {
                max_size += current_entry_size;
                buf_offset += current_entry_size;
            }
            else
            {
                is_chunk_fit = false;
            }
        }
        else
        {
            is_chunk_fit = false;
        }
    }

    return max_size;
}

/**
 * @brief Flush flash callback for timer
 *
 * @details Flush buffer to flash when triggered by the timer
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 */
static void timer_flush_flash_internal (void *logger_ptr)
{
    blackbox_logger_flush ((blackbox_logger_t *) logger_ptr);
}

/**
 * @brief Update Logger configuration parameters structure
 *
 * @details Update configuration parameters if logger parameters
 * changed at any point
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 *
 * @return blackbox_logger_err_t
 * @retval BLACKBOX_LOGGER_OK Update success
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 */
static blackbox_logger_err_t update_parameters_internal (const blackbox_logger_t *logger_ptr)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;
    if ((logger_ptr == NULL) || (logger_ptr->config_ptr == NULL))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }
    else
    {
        logger_ptr->config_ptr->parameters.write_offset  = logger_ptr->write_offset;
        logger_ptr->config_ptr->parameters.last_id       = logger_ptr->last_id;
        logger_ptr->config_ptr->parameters.replay_offset = logger_ptr->replay_offset;
        logger_ptr->config_ptr->parameters.is_dirty      = true;
    }

    return err;
}

/**
 * @brief Maps esp errors to logger errors
 *
 * @param err ESP error
 *
 * @return blackbox_logger_err_t Blackbox Logger error
 */
static blackbox_logger_err_t esp_to_blackbox_error_map (const esp_err_t err)
{
    blackbox_logger_err_t bb_err = BLACKBOX_LOGGER_ERR_FAIL;

    switch (err)
    {
    case ESP_OK:
        bb_err = BLACKBOX_LOGGER_OK;
        break;
    case ESP_ERR_INVALID_ARG:
        bb_err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
        break;
    case ESP_ERR_NO_MEM:
        bb_err = BLACKBOX_LOGGER_ERR_NO_MEM;
        break;
    case ESP_ERR_TIMEOUT:
        bb_err = BLACKBOX_LOGGER_ERR_TIMEOUT;
        break;
    case ESP_ERR_INVALID_CRC:
        bb_err = BLACKBOX_LOGGER_ERR_INVALID_CRC;
        break;
    case ESP_FAIL:
        bb_err = BLACKBOX_LOGGER_ERR_FAIL;
        break;
    default:
        bb_err = BLACKBOX_LOGGER_ERR_FAIL;
        break;
    }

    return bb_err;
}

/**
 * @brief Binary search partition sectors beginnings to find maximum ID
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 *
 * @return blackbox_logger_err_t Search result
 * @retval BLACKBOX_LOGGER_OK Search success
 * @retval BLACKBOX_LOGGER_ERR_FAIL Search fail
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 *
 * @note Logger's parameters instance updated on success
 */
static blackbox_logger_err_t binary_search_recover_helper (const blackbox_logger_t *logger_ptr)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;

    if ((logger_ptr == NULL) || (logger_ptr->config_ptr == NULL) ||
        (logger_ptr->config_ptr->read_func == NULL) || (logger_ptr->config_ptr->partition_ptr == NULL) ||
        (logger_ptr->config_ptr->sector_buf_size == 0U))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        int32_t low_sector = 0;
        int32_t high_sector =
            (logger_ptr->config_ptr->partition_ptr->size / logger_ptr->config_ptr->sector_buf_size) - 1;
        int32_t head_sector = 0;
        uint32_t base_id    = 0;
        uint32_t max_id     = 0;

        blackbox_logger_entry_header_t current_entry_header;
        esp_err_t read_err = logger_ptr->config_ptr->read_func (
            logger_ptr->config_ptr->partition_ptr, 0, &current_entry_header, sizeof (current_entry_header));
        err = esp_to_blackbox_error_map (read_err);

        if (read_err == ESP_OK)
        {
            if (current_entry_header.header_magic == BLACKBOX_LOGGER_HEADER_MAGIC)
            {
                base_id = current_entry_header.id;
                max_id  = base_id;
            }
            else
            {
                // First sector is empty or junk
                // We just use 0 as base and let the loop find data
                base_id = 0;
                max_id  = 0;
            }
        }

        while ((low_sector <= high_sector) && (err == BLACKBOX_LOGGER_OK))
        {
            int32_t mid_sector = low_sector + (high_sector - low_sector) / 2;

            read_err = logger_ptr->config_ptr->read_func (
                logger_ptr->config_ptr->partition_ptr, (mid_sector * logger_ptr->config_ptr->sector_buf_size),
                &current_entry_header, sizeof (current_entry_header));
            err = esp_to_blackbox_error_map (read_err);

            if (err != BLACKBOX_LOGGER_OK)
            {
                // Read error
                err = BLACKBOX_LOGGER_ERR_FAIL;
            }
            else if (current_entry_header.header_magic == BLACKBOX_LOGGER_HEADER_MAGIC)
            {
                uint32_t mid_id = current_entry_header.id;

                if (is_logically_next_in_order_helper (mid_id, base_id))
                {
                    head_sector = mid_sector;
                    low_sector  = mid_sector + 1;
                    max_id      = mid_id;
                }
                else
                {
                    high_sector = mid_sector - 1;
                }
            }
            else
            {
                // Empty flash: the data ends before this point or
                // Corruption: treat as "End of Data"
                high_sector = mid_sector - 1;
            }
        }

        if (err == BLACKBOX_LOGGER_OK)
        {
            logger_ptr->config_ptr->parameters.write_offset =
                (head_sector * logger_ptr->config_ptr->sector_buf_size);
            logger_ptr->config_ptr->parameters.last_id = max_id;
        }
    }

    return err;
}

/**
 * @brief Search linearly from given logger parameters until maximum ID is found
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 *
 * @return blackbox_logger_err_t Search result
 * @retval BLACKBOX_LOGGER_OK Search success
 * @retval BLACKBOX_LOGGER_ERR_FAIL Search fail
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 *
 * @note Logger's parameters instance updated on success
 */
static blackbox_logger_err_t linear_recover_helper (const blackbox_logger_t *logger_ptr)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;

    if ((logger_ptr == NULL) || (logger_ptr->sector_buf_lock == NULL) || (logger_ptr->config_ptr == NULL) ||
        (logger_ptr->config_ptr->sector_buf_ptr == NULL) || (logger_ptr->config_ptr->read_func == NULL) ||
        (logger_ptr->config_ptr->partition_ptr == NULL) ||
        (logger_ptr->config_ptr->partition_ptr->size == 0) || (logger_ptr->config_ptr->sector_buf_size == 0))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    bool is_lock_taken = false;
    if (err == BLACKBOX_LOGGER_OK)
    {
        if (xSemaphoreTake (logger_ptr->sector_buf_lock,
                            pdMS_TO_TICKS (BLACKBOX_LOGGER_SECTOR_LOCK_TIMEOUT_MS)) != pdTRUE)
        {
            err = BLACKBOX_LOGGER_ERR_TIMEOUT;
        }
        else
        {
            is_lock_taken = true;
        }
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        size_t highest_id   = logger_ptr->config_ptr->parameters.last_id;
        size_t write_offset = logger_ptr->config_ptr->parameters.write_offset;
        uint32_t current_sector =
            (logger_ptr->config_ptr->parameters.write_offset / logger_ptr->config_ptr->sector_buf_size);
        // Track the starting sector to prevent infinite wrap-around loops
        const size_t total_sectors =
            (logger_ptr->config_ptr->partition_ptr->size / logger_ptr->config_ptr->sector_buf_size);
        // Needed before while loop to check if we exceeded partition size
        uint32_t current_sector_base_offset = (current_sector * logger_ptr->config_ptr->sector_buf_size);
        // Needed after while loop to calculate write offset
        // So it is initialized to zero as default
        uint32_t current_buf_pos =
            (logger_ptr->config_ptr->parameters.write_offset % logger_ptr->config_ptr->sector_buf_size);
        uint32_t sectors_processed = 0;
        bool scan_complete         = false;

        // Start of linear scan logic
        while ((scan_complete != true) && (sectors_processed < total_sectors))
        {
            current_sector_base_offset = (current_sector * logger_ptr->config_ptr->sector_buf_size);
            esp_err_t read_err         = logger_ptr->config_ptr->read_func (
                logger_ptr->config_ptr->partition_ptr, current_sector_base_offset,
                logger_ptr->config_ptr->sector_buf_ptr, logger_ptr->config_ptr->sector_buf_size);
            err = esp_to_blackbox_error_map (read_err);

            if (err != BLACKBOX_LOGGER_OK)
            {
                scan_complete = true;
            }
            else
            {
                const uint8_t *const buf = logger_ptr->config_ptr->sector_buf_ptr;

                while (((current_buf_pos + sizeof (blackbox_logger_entry_header_t)) <=
                        logger_ptr->config_ptr->sector_buf_size) &&
                       (scan_complete != true))
                {
                    err = validate_entry_internal (
                        &buf[current_buf_pos], (logger_ptr->config_ptr->sector_buf_size - current_buf_pos));
                    if (err == BLACKBOX_LOGGER_OK)
                    {
                        // Entry is valid. We need the ID from the header to track the 'highest_id'
                        const blackbox_logger_entry_header_t *header_ptr =
                            (blackbox_logger_entry_header_t *) &buf[current_buf_pos];
                        uint32_t entry_size = entry_total_aligned_size_helper (
                            header_ptr->length, logger_ptr->config_ptr->address_align);
                        if (is_logically_next_in_order_helper (header_ptr->id, highest_id) == true)
                        {
                            highest_id = header_ptr->id;
                            // Set write_offset to the NEXT available slot
                            write_offset = (current_sector_base_offset + current_buf_pos + entry_size);
                            // Advance position by the entry size
                            current_buf_pos += entry_size;
                        }
                        else
                        {
                            // This entry is older than the previous one
                            scan_complete = true;
                        }
                    }
                    else
                    {
                        // We hit an invalid entry (Torn write or Empty Flash)
                        // This is NOT a hard error for the recovery process
                        // but it is the end of it
                        err           = BLACKBOX_LOGGER_OK;
                        scan_complete = true;
                    }
                }
            }

            // Reset for next sector round
            current_buf_pos = 0U;
            // handle sector index with wrapping
            current_sector = (current_sector + 1) % total_sectors;
            ++sectors_processed;
        }

        // End of linear scan logic
        // Check if it succeeded and update parameters if so
        if (err == BLACKBOX_LOGGER_OK)
        {
            logger_ptr->config_ptr->parameters.write_offset = write_offset;
            logger_ptr->config_ptr->parameters.last_id      = highest_id;
        }
    }

    if (is_lock_taken == true)
    {
        (void) xSemaphoreGive (logger_ptr->sector_buf_lock);
    }

    return err;
}

/**
 * @brief Recover Logger parameters
 *
 * @details Recover Logger parameters first using provided parameters if
 * not valid it binary searches for 'head sector' a sector which has the
 * highest entry ID at it's beginning then linearly search that 'head sector'
 * for the highest ID
 *
 * @param logger_ptr Pointer to BlackBox Logger instance
 *
 * @return blackbox_logger_err_t Recover result
 * @retval BLACKBOX_LOGGER_OK Recover success
 * @retval BLACKBOX_LOGGER_ERR_FAIL Recover fail
 * @retval BLACKBOX_LOGGER_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval BLACKBOX_LOGGER_ERR_TIMEOUT Timed out waiting for resource
 */
static blackbox_logger_err_t recover_logger_internal (blackbox_logger_t *logger_ptr)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;

    if ((logger_ptr == NULL) || (logger_ptr->config_ptr == NULL) ||
        (logger_ptr->config_ptr->read_func == NULL) || (logger_ptr->config_ptr->partition_ptr == NULL))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        // Check if NVS hint was the default value. We check write offset
        // only because whether it was the default or genuine hint the
        // recovery will converge faster in worst case of 'bad hint'
        err = ((logger_ptr->config_ptr->parameters.write_offset ==
                blackbox_logger_default_parameters.write_offset)
                   ? BLACKBOX_LOGGER_ERR_INVALID_ARG
                   : BLACKBOX_LOGGER_OK);

        if (err == BLACKBOX_LOGGER_OK)
        {
            // Try using the hint
            blackbox_logger_entry_header_t header;

            if (logger_ptr->config_ptr->read_func (logger_ptr->config_ptr->partition_ptr,
                                                   logger_ptr->config_ptr->parameters.write_offset, &header,
                                                   sizeof (header)) == ESP_OK &&
                header.header_magic == BLACKBOX_LOGGER_HEADER_MAGIC)
            {
                // Hint point to a valid entry header - We are in 'Forward Recovery' mode
                err = linear_recover_helper (logger_ptr);
            }
            else
            {
                // Hint pointed to invalid entry header
                err = BLACKBOX_LOGGER_ERR_FAIL;
            }
        }

        if (err != BLACKBOX_LOGGER_OK)
        {
            // Hint failed, NVS is out of sync or corrupted
            // Performing 'Binary Search' to find where the 'Head Sector' is
            // and ID sequence breaks
            err = binary_search_recover_helper (logger_ptr);
            if (err == BLACKBOX_LOGGER_OK)
            {
                // Find highest ID in 'Forward Recovery' mode starting at 'Head Sector'
                err = linear_recover_helper (logger_ptr);
                if (err != BLACKBOX_LOGGER_OK)
                {
                    // Finally: Fallback to default parameters
                    logger_ptr->config_ptr->parameters = blackbox_logger_default_parameters;
                }
            }
        }

        // Fill-in recovered logger parameters
        logger_ptr->write_offset  = logger_ptr->config_ptr->parameters.write_offset;
        logger_ptr->replay_offset = logger_ptr->config_ptr->parameters.replay_offset;
        logger_ptr->last_id       = logger_ptr->config_ptr->parameters.last_id;

        // Update parameters to be marked as 'dirty'
        (void) update_parameters_internal (logger_ptr);
    }

    return err;
}

blackbox_logger_err_t blackbox_logger_init (blackbox_logger_t *logger_ptr,
                                            blackbox_logger_config_t *config_ptr)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;

    if ((logger_ptr == NULL) || (config_ptr == NULL))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        logger_ptr->config_ptr         = config_ptr;
        logger_ptr->last_erased_sector = -1;
        logger_ptr->batch_len          = 0;

        logger_ptr->write_lock = xSemaphoreCreateMutex ();
        if (logger_ptr->write_lock == NULL)
        {
            err = BLACKBOX_LOGGER_ERR_NO_MEM;
        }
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        logger_ptr->sector_buf_lock = xSemaphoreCreateMutex ();
        if (logger_ptr->sector_buf_lock == NULL)
        {
            err = BLACKBOX_LOGGER_ERR_NO_MEM;
        }
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        // Setup one-shot flush timer
        const esp_timer_create_args_t timer_args = {
            .callback = timer_flush_flash_internal, .arg = (void *) logger_ptr, .name = "flash_flush_timer"};

        esp_err_t tim_err = esp_timer_create (&timer_args, &logger_ptr->flush_timer);
        err               = esp_to_blackbox_error_map (tim_err);
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        // If recover was not successful means Logger was reset with defaults
        (void) recover_logger_internal (logger_ptr);
    }

    if (err != BLACKBOX_LOGGER_OK)
    {
        ESP_LOGE (TAG, "Initialization error with code: %" PRIu8, err);
        blackbox_logger_deinit (logger_ptr);
    }
    else
    {
        ESP_LOGI (TAG, "Initialized");
    }

    return err;
}

blackbox_logger_err_t blackbox_logger_write (blackbox_logger_t *logger_ptr,
                                             const blackbox_logger_entry_view_t *entry_ptr)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;
    bool is_lock_taken        = false;

    if ((logger_ptr == NULL) || (logger_ptr->write_lock == NULL) || (logger_ptr->flush_timer == NULL) ||
        (logger_ptr->config_ptr == NULL) || (logger_ptr->config_ptr->batch_buf_ptr == NULL) ||
        (entry_ptr == NULL) || (entry_ptr->data_ptr == NULL) || (entry_ptr->length == 0))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        const uint32_t entry_size =
            entry_total_aligned_size_helper (entry_ptr->length, logger_ptr->config_ptr->address_align);

        // Check for buffer overflow
        if (entry_size > logger_ptr->config_ptr->batch_buf_size)
        {
            // Won't fit in buffer
            err = BLACKBOX_LOGGER_ERR_NO_MEM;
        }
        else if ((logger_ptr->batch_len + entry_size) > logger_ptr->config_ptr->batch_buf_size)
        {
            // Flush since this write would overflow the buffer
            (void) esp_timer_stop (logger_ptr->flush_timer);
            err = blackbox_logger_flush (logger_ptr);
        }
        else
        {
            // We are good to go
        }

        if (err == BLACKBOX_LOGGER_OK)
        {
            if (xSemaphoreTake (logger_ptr->write_lock,
                                pdMS_TO_TICKS (BLACKBOX_LOGGER_WRITE_LOCK_TIMEOUT_MS)) != pdTRUE)
            {
                err = BLACKBOX_LOGGER_ERR_TIMEOUT;
            }
            else
            {
                is_lock_taken = true;
            }
        }

        if (err == BLACKBOX_LOGGER_OK)
        {
            // Write entry to memory buffer: header -> data -> tail
            const blackbox_logger_entry_header_t entry_header = {.header_magic = BLACKBOX_LOGGER_HEADER_MAGIC,
                                                                 .id           = (logger_ptr->last_id + 1U),
                                                                 .length       = entry_ptr->length};

            uint8_t *const base_ptr = (uint8_t *) logger_ptr->config_ptr->batch_buf_ptr;
            uint8_t *write_ptr      = &base_ptr[logger_ptr->batch_len];

            (void) memcpy (write_ptr, &entry_header, sizeof (entry_header));
            write_ptr = &write_ptr[sizeof (entry_header)];

            (void) memcpy (write_ptr, entry_ptr->data_ptr, entry_ptr->length);
            write_ptr = &write_ptr[entry_ptr->length];

            const blackbox_logger_entry_tail_t entry_tail = {
                .crc        = calculate_modbus_crc16 (&base_ptr[logger_ptr->batch_len],
                                                      sizeof (entry_header) + entry_ptr->length),
                .tail_magic = BLACKBOX_LOGGER_TAIL_MAGIC};
            (void) memcpy (write_ptr, &entry_tail, sizeof (entry_tail));

            // Logger state update
            logger_ptr->batch_len += entry_size;
            ++(logger_ptr->last_id);

            // Reset timer
            (void) esp_timer_stop (logger_ptr->flush_timer);
            (void) esp_timer_start_once (logger_ptr->flush_timer,
                                         logger_ptr->config_ptr->flush_timer_timeout_us);
        }
    }

    if (is_lock_taken == true)
    {
        (void) xSemaphoreGive (logger_ptr->write_lock);
    }

    return err;
}

blackbox_logger_err_t blackbox_logger_read (const blackbox_logger_t *logger_ptr,
                                            const uint32_t read_offset,
                                            blackbox_logger_entry_view_t *entry_ptr,
                                            const size_t entry_buf_max_capacity)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;
    bool is_lock_taken        = false;

    if ((logger_ptr == NULL) || (logger_ptr->sector_buf_lock == NULL) || (logger_ptr->config_ptr == NULL) ||
        (logger_ptr->config_ptr->sector_buf_ptr == NULL) || (logger_ptr->config_ptr->read_func == NULL) ||
        (entry_ptr == NULL) || (entry_ptr->data_ptr == NULL) ||
        (read_offset >= logger_ptr->config_ptr->partition_ptr->size))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        if (xSemaphoreTake (logger_ptr->sector_buf_lock,
                            pdMS_TO_TICKS (BLACKBOX_LOGGER_SECTOR_LOCK_TIMEOUT_MS)) != pdTRUE)
        {
            err = BLACKBOX_LOGGER_ERR_TIMEOUT;
        }
        else
        {
            is_lock_taken = true;
        }
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        // Calculate a safe read size
        uint32_t read_size             = logger_ptr->config_ptr->batch_buf_size;
        const uint32_t remaining_space = logger_ptr->config_ptr->partition_ptr->size - read_offset;

        if (read_size > remaining_space)
        {
            read_size = remaining_space;
        }

        // Execute hardware read
        esp_err_t read_err =
            logger_ptr->config_ptr->read_func (logger_ptr->config_ptr->partition_ptr, read_offset,
                                               logger_ptr->config_ptr->sector_buf_ptr, read_size);
        err = esp_to_blackbox_error_map (read_err);

        if (err == BLACKBOX_LOGGER_OK)
        {
            // Validate entry
            err = validate_entry_internal (logger_ptr->config_ptr->sector_buf_ptr, read_size);
        }

        if (err == BLACKBOX_LOGGER_OK)
        {
            const blackbox_logger_entry_header_t *entry_header_ptr =
                (blackbox_logger_entry_header_t *) logger_ptr->config_ptr->sector_buf_ptr;

            if (entry_header_ptr->length > entry_buf_max_capacity)
            {
                // Valid entry but doesn't fit in provided buffer
                err = BLACKBOX_LOGGER_ERR_NO_MEM;
            }
            else
            {
                // Copy entry to provided buffer
                (void) memcpy (entry_ptr->data_ptr,
                               logger_ptr->config_ptr->sector_buf_ptr +
                                   sizeof (blackbox_logger_entry_header_t),
                               entry_header_ptr->length);
                entry_ptr->length = entry_header_ptr->length;
            }
        }
    }

    if (is_lock_taken == true)
    {
        (void) xSemaphoreGive (logger_ptr->sector_buf_lock);
    }

    return err;
}

blackbox_logger_err_t blackbox_logger_flush (blackbox_logger_t *logger_ptr)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;
    bool is_lock_taken        = false;

    if ((logger_ptr == NULL) || (logger_ptr->write_lock == NULL) || (logger_ptr->config_ptr == NULL) ||
        (logger_ptr->config_ptr->partition_ptr == NULL) || (logger_ptr->config_ptr->write_func == NULL) ||
        (logger_ptr->config_ptr->erase_func == NULL) || (logger_ptr->config_ptr->batch_buf_ptr == NULL) ||
        (logger_ptr->batch_len == 0U) || (logger_ptr->config_ptr->sector_buf_size == 0U))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        if (xSemaphoreTake (logger_ptr->write_lock, pdMS_TO_TICKS (BLACKBOX_LOGGER_WRITE_LOCK_TIMEOUT_MS)) !=
            pdTRUE)
        {
            err = BLACKBOX_LOGGER_ERR_TIMEOUT;
        }
        else
        {
            is_lock_taken = true;
        }
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        uint32_t write_offset_old = logger_ptr->write_offset;
        uint32_t write_offset     = logger_ptr->write_offset;
        uint32_t replay_offset    = logger_ptr->replay_offset;
        uint32_t buffer_offset    = 0;
        uint32_t write_size       = logger_ptr->batch_len;

        do
        {
            int32_t current_write_sector  = (write_offset / logger_ptr->config_ptr->sector_buf_size);
            int32_t current_replay_sector = (replay_offset / logger_ptr->config_ptr->sector_buf_size);
            uint32_t next_sector_start = (current_write_sector + 1) * logger_ptr->config_ptr->sector_buf_size;
            uint32_t remaining_sector_space = (next_sector_start - write_offset);

            if (next_sector_start >= logger_ptr->config_ptr->partition_ptr->size)
            {
                next_sector_start = 0;
            }

            // Handle tail-biting (write/replay offsets encounter)
            if ((current_write_sector == current_replay_sector) && (replay_offset > write_offset))
            {
#if CONFIG_ENABLE_BLACKBOX_LOGGER_WRITING_OVER_OLD_ENTRIES == 1
                // Move replay offset to next sector 's beginning
                // Since no entry spans sectors so possible entry present at begining
                replay_offset = ((current_replay_sector + 1) * logger_ptr->config_ptr->sector_buf_size);
                if (replay_offset >= logger_ptr->config_ptr->partition_ptr->size)
                {
                    replay_offset = 0;
                }
#else
                err = BLACKBOX_LOGGER_ERR_NO_MEM;
#endif
            }

            if (err == BLACKBOX_LOGGER_OK)
            {
                uint32_t current_write_size = get_entries_chunk_size_with_ceiling_helper (
                    &logger_ptr->config_ptr->batch_buf_ptr[buffer_offset], write_size, remaining_sector_space,
                    logger_ptr->config_ptr->address_align);
                if (current_write_size > 0)
                {
                    if (current_write_sector != logger_ptr->last_erased_sector)
                    {
                        esp_err_t erase_err = logger_ptr->config_ptr->erase_func (
                            logger_ptr->config_ptr->partition_ptr,
                            (current_write_sector * logger_ptr->config_ptr->sector_buf_size),
                            logger_ptr->config_ptr->sector_buf_size);
                        logger_ptr->last_erased_sector = current_write_sector;
                        err                            = esp_to_blackbox_error_map (erase_err);
                    }

                    if (err != BLACKBOX_LOGGER_OK)
                    {
                        err = BLACKBOX_LOGGER_ERR_ERASE_FAIL;
                    }
                    else
                    {
                        esp_err_t write_err = logger_ptr->config_ptr->write_func (
                            logger_ptr->config_ptr->partition_ptr, write_offset,
                            &logger_ptr->config_ptr->batch_buf_ptr[buffer_offset], current_write_size);
                        err = esp_to_blackbox_error_map (write_err);

                        if (err != BLACKBOX_LOGGER_OK)
                        {
                            err = BLACKBOX_LOGGER_ERR_WRITE_FAIL;
                        }
                        else
                        {
                            write_offset += current_write_size;
                            write_size -= current_write_size;
                            buffer_offset += current_write_size;
                        }
                    }
                }
                else
                {
                    // No write can be made with available space
                    if (remaining_sector_space < logger_ptr->config_ptr->sector_buf_size)
                    {
                        write_offset = next_sector_start;
                    }
                    else
                    {
                        // Current entry size is bigger than sector size
                        err = BLACKBOX_LOGGER_ERR_NO_MEM;
                    }
                }

                if (err == BLACKBOX_LOGGER_OK)
                {
                    logger_ptr->replay_offset = replay_offset;
                    write_offset_old          = logger_ptr->write_offset;
                    logger_ptr->write_offset  = write_offset;
                }
            }
        } while ((write_size > 0) && (err == BLACKBOX_LOGGER_OK));

        logger_ptr->replay_offset = replay_offset;
        logger_ptr->write_offset  = write_offset_old;
        // Update parameters before moving write_offset since we need
        // last successful write offset for recover
        (void) update_parameters_internal (logger_ptr);
        logger_ptr->write_offset = write_offset;

        uint32_t bytes_written   = buffer_offset;
        uint32_t bytes_remaining = logger_ptr->batch_len - bytes_written;
        if ((bytes_remaining > 0) && (bytes_written > 0))
        {
            (void) memmove (logger_ptr->config_ptr->batch_buf_ptr,
                            &logger_ptr->config_ptr->batch_buf_ptr[bytes_written], bytes_remaining);
        }
        logger_ptr->batch_len = bytes_remaining;

        // Check if replay available and notify registered callback
        if ((logger_ptr->config_ptr->on_replay_available_func != NULL) && (bytes_written > 0))
        {
            logger_ptr->config_ptr->on_replay_available_func ();
        }
    }

    if (is_lock_taken == true)
    {
        (void) xSemaphoreGive (logger_ptr->write_lock);
    }

    return err;
}

blackbox_logger_err_t blackbox_logger_next_replay (blackbox_logger_t *logger_ptr,
                                                   blackbox_logger_iter_cb_t is_entry_processed_cb_func,
                                                   void *cb_arg_void_ptr,
                                                   blackbox_logger_entry_view_t *entry_ptr,
                                                   const size_t entry_buf_max_capacity)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;

    if ((logger_ptr == NULL) || (is_entry_processed_cb_func == NULL) || (entry_ptr == NULL))
    {
        err = BLACKBOX_LOGGER_ERR_INVALID_ARG;
    }

    if (err == BLACKBOX_LOGGER_OK)
    {
        // Check if replay entries exist
        if (blackbox_logger_has_replay_data (logger_ptr) == true)
        {
            // Read current entry
            err = blackbox_logger_read (logger_ptr, logger_ptr->replay_offset, entry_ptr,
                                        entry_buf_max_capacity);

            if (err != BLACKBOX_LOGGER_OK)
            {
                // If invalid entry, sync offset to "empty" the buffer
                logger_ptr->replay_offset = logger_ptr->write_offset;
            }
            else
            {
                // Process entry via callback
                bool is_processed = is_entry_processed_cb_func (cb_arg_void_ptr, entry_ptr);
                if (is_processed == true)
                {
                    // Advance offset using aligned size
                    uint32_t total_size = entry_total_aligned_size_helper (
                        entry_ptr->length, logger_ptr->config_ptr->address_align);
                    logger_ptr->replay_offset += total_size;
                    err = BLACKBOX_LOGGER_OK;
                }
                else
                {
                    err = BLACKBOX_LOGGER_ERR_PROCESS_FAIL;
                }
            }
        }
        else
        {
            err = BLACKBOX_LOGGER_ERR_INVALID_ENTRY;
        }
    }

    return err;
}

blackbox_logger_err_t blackbox_logger_iterate_replay (blackbox_logger_t *logger_ptr,
                                                      blackbox_logger_iter_cb_t is_entry_processed_cb_func,
                                                      void *cb_arg_void_ptr,
                                                      blackbox_logger_entry_view_t *entry_ptr,
                                                      const size_t entry_buf_max_capacity)
{
    blackbox_logger_err_t err = BLACKBOX_LOGGER_OK;

    while (err == BLACKBOX_LOGGER_OK)
    {
        err = blackbox_logger_next_replay (logger_ptr, is_entry_processed_cb_func, cb_arg_void_ptr, entry_ptr,
                                           entry_buf_max_capacity);
    }

    return err;
}

void blackbox_logger_get_parameters_defaults (blackbox_logger_parameters_t *parameter_ptr)
{
    if (parameter_ptr != NULL)
    {
        parameter_ptr->write_offset  = blackbox_logger_default_parameters.write_offset;
        parameter_ptr->replay_offset = blackbox_logger_default_parameters.replay_offset;
        parameter_ptr->last_id       = blackbox_logger_default_parameters.last_id;
        parameter_ptr->is_dirty      = blackbox_logger_default_parameters.is_dirty;
    }
}

bool blackbox_logger_has_replay_data (const blackbox_logger_t *logger_ptr)
{
    bool has_data = false;
    if (logger_ptr != NULL)
    {
        has_data = (logger_ptr->write_offset != logger_ptr->replay_offset);
    }

    return has_data;
}

void blackbox_logger_deinit (blackbox_logger_t *logger_ptr)
{
    if (logger_ptr != NULL)
    {
        if (logger_ptr->flush_timer != NULL)
        {
            // Stop and delete timer
            (void) esp_timer_stop (logger_ptr->flush_timer);
            (void) esp_timer_delete (logger_ptr->flush_timer);
            logger_ptr->flush_timer = NULL;
        }

        // Flush data
        (void) blackbox_logger_flush (logger_ptr);

        // Free lock(s) resources
        if (logger_ptr->write_lock != NULL)
        {
            vSemaphoreDelete (logger_ptr->write_lock);
            logger_ptr->write_lock = NULL;
        }

        if (logger_ptr->sector_buf_lock != NULL)
        {
            vSemaphoreDelete (logger_ptr->sector_buf_lock);
            logger_ptr->sector_buf_lock = NULL;
        }

        logger_ptr->config_ptr         = NULL;
        logger_ptr->write_offset       = 0;
        logger_ptr->replay_offset      = 0;
        logger_ptr->last_id            = 0;
        logger_ptr->last_erased_sector = -1;
        logger_ptr->batch_len          = 0;
    }
}
