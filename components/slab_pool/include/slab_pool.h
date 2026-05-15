/**
 * @file slab_pool.h
 * @ingroup utilities_module
 * @author Abanoub Salah
 * @brief Memory slab provide abstraction layer for memory chunks
 *
 * @details
 * - Thread-safe interface
 * - Designed to provide ready memory-pool to any layer.
 */

#ifndef SLAB_POOL_H
#define SLAB_POOL_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Slab pool size */
#define SLAB_POOL_SIZE (32U)
/** Slab pool data maximum size */
#define SLAB_POOL_MAX_DATA_SIZE (512U)

/**
 * @brief Slab pool type structure
 */
typedef struct {
    uint8_t data[SLAB_POOL_MAX_DATA_SIZE]; /**< Slab data buffer */
    uint16_t length;                       /**< Slab data buffer 'current held data' length */
} slab_pool_t;

/**
 * @brief Initialize slab pool
 *
 * @return esp_err_t Initialize result
 * @retval ESP_OK Initialize success
 * @retval ESP_ERR_NO_MEM No available memory for resource allocation
 * @retval ESP_ERR_INVALID_STATE Already initialized
 */
esp_err_t slab_pool_init (void);

/**
 * @brief Allocate a slab from pool
 *
 * @return slab_pool_t* pointer to slab on success otherwise NULL
 */
slab_pool_t *slab_pool_alloc (void);

/**
 * @brief Return slab to pool
 *
 * @param slab_ptr Pointer to slab structure
 */
void slab_pool_free (slab_pool_t *slab_ptr);

/**
 * @brief Deinitialize slab pool
 */
void slab_pool_deinit (void);

#endif
