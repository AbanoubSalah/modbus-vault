/**
 * @file serializer.h
 * @ingroup utilities_module
 * @author Abanoub Salah
 * @brief Data serialization layer
 *
 * @details
 * - Features:
 *     - Pack data to be publish-ready
 *     - Fast packing using protobufc
 * - Designed to abstract low-level from application logic
 */

#ifndef SERIALIZER_H
#define SERIALIZER_H

#include "esp_err.h"
#include "slab_pool.h"

#include <stdint.h>

/**
 * @brief Serialize raw data
 *
 * @param src_ptr Pointer to raw data
 * @param timestamp_us Raw data timestamp
 * @param dst_ptr Pointer to packing buffer
 *
 * @return esp_err_t
 * @retval ESP_OK Serialize success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_NO_MEM No available space in destination
 */
esp_err_t serializer_pack (const slab_pool_t *src_ptr, int64_t timestamp_us, slab_pool_t *dst_ptr);

#endif
