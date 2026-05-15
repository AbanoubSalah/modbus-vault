/**
 * @file modbus_slicer.c
 * @ingroup modbus_analyzer_module
 * @brief Implementation of the modbus slicer
 *
 * @details
 * - Pre-calculates 3.5 characters timeout
 * - Uses "slab pool" for buffering frames and passes it to consumers
 * - Copies arriving bytes to buffer
 * - Calls registered callback function on data/error
 */

#include "modbus_slicer.h"

#include <string.h>

/** Micro-seconds in one second */
#define MODBUS_SLICER_USECONDS_PER_SECOND (1000000UL)
/** Multiply a num by 3.5 */
#define MODBUS_SLICER_INT_MULTIPLY_BY_3_5(num) ((7 * num) / 2)
/** Multiply a num by 20% or (1/5) */
#define MODBUS_SLICER_INT_CALCULATE_20_PERCENT(num) ((num) / 5)

void modbus_slicer_init (modbus_slicer_t *slicer_ptr, modbus_slicer_config_t *config_ptr)
{
    slicer_ptr->config_ptr = config_ptr;

    // Calculate 3.5 characters time
    int64_t t1          = (config_ptr->bit_length * MODBUS_SLICER_USECONDS_PER_SECOND / config_ptr->baudrate);
    slicer_ptr->t3_5_us = MODBUS_SLICER_INT_MULTIPLY_BY_3_5 (t1);
    slicer_ptr->t3_5_us += MODBUS_SLICER_INT_CALCULATE_20_PERCENT (slicer_ptr->t3_5_us);
}

void modbus_slicer_feed (modbus_slicer_t *slicer_ptr,
                         const uint8_t *data_ptr,
                         size_t length,
                         int64_t timestamp_us)
{
    if ((slicer_ptr != NULL) && (data_ptr != NULL))
    {
        modbus_slicer_error_t err = MODBUS_SLICER_OK;
        if (length != 0U)
        {
            // Data present
            if (slicer_ptr->slab_ptr == NULL)
            {
                // Start of frame, acquire a slab for buffering
                slicer_ptr->slab_ptr           = slab_pool_alloc ();
                slicer_ptr->start_timestamp_us = timestamp_us;
                if (slicer_ptr->slab_ptr == NULL)
                {
                    err = MODBUS_SLICER_ERROR_NO_MEM;
                }
                else
                {
                    // Initialize it
                    slicer_ptr->slab_ptr->length = 0;
                }
            }

            if (err == MODBUS_SLICER_OK)
            {
                if ((slicer_ptr->slab_ptr->length + length) > SLAB_POOL_MAX_DATA_SIZE)
                {
                    // overflow -> drop frame
                    err = MODBUS_SLICER_ERROR_OVERFLOW;
                    // Set length to zero for reusing it for next frame
                    slicer_ptr->slab_ptr->length = 0U;
                }
                else
                {
                    (void) memcpy (&slicer_ptr->slab_ptr->data[slicer_ptr->slab_ptr->length], data_ptr,
                                   length);
                    slicer_ptr->slab_ptr->length += length;
                }
            }

            if (err != MODBUS_SLICER_OK)
            {
                // Note: No need to free memory here, it is used in next frame if present
                // so we won't pass it to callback
                modbus_slicer_frame_t slicer_frame = {
                    .slab_ptr = NULL, .timestamp_us = slicer_ptr->start_timestamp_us, .error = err};
                if (slicer_ptr->config_ptr->on_frame_func)
                {
                    // Trigger frame callback
                    slicer_ptr->config_ptr->on_frame_func (&slicer_frame,
                                                           slicer_ptr->config_ptr->cb_arg_void_ptr);
                }
            }
        }
        slicer_ptr->last_byte_timestamp_us = timestamp_us;
    }
}

void modbus_slicer_timeout (modbus_slicer_t *slicer_ptr)
{
    if ((slicer_ptr != NULL) && (slicer_ptr->slab_ptr != NULL) && (slicer_ptr->slab_ptr->length > 0U))
    {
        // Data present
        if ((slicer_ptr->config_ptr != NULL) && (slicer_ptr->config_ptr->on_frame_func != NULL))
        {
            modbus_slicer_frame_t slicer_frame = {.slab_ptr     = slicer_ptr->slab_ptr,
                                                  .timestamp_us = slicer_ptr->start_timestamp_us,
                                                  .error        = MODBUS_SLICER_OK};
            slicer_ptr->config_ptr->on_frame_func (&slicer_frame, slicer_ptr->config_ptr->cb_arg_void_ptr);
            slicer_ptr->slab_ptr = NULL;
        }
        else
        {
            // No callback -> frame dropped
            // Keeping slab for next frame
            slicer_ptr->slab_ptr->length = 0;
        }
    }
}

void modbus_slicer_check_timeout (modbus_slicer_t *slicer_ptr)
{
    if ((slicer_ptr != NULL) && (slicer_ptr->config_ptr != NULL) &&
        (slicer_ptr->config_ptr->get_time_us_func != NULL))
    {
        int64_t now   = slicer_ptr->config_ptr->get_time_us_func ();
        int64_t delta = now - slicer_ptr->last_byte_timestamp_us;

        if ((slicer_ptr->slab_ptr != NULL) && (slicer_ptr->slab_ptr->length > 0U) &&
            (delta > slicer_ptr->t3_5_us))
        {
            // Timed out (3.5 characters)
            modbus_slicer_timeout (slicer_ptr);
        }
    }
}
