/**
 * @file slab_pool.c
 * @ingroup utilities_module
 * @brief Implementation of the slab pool
 *
 * @details
 * - Uses a static pool of slabs
 * - Manage available slabs using singly linked list with a lock for mutual access
 * - On slab alloc returns list head if available
 * - On slab free make the returned slab the new head
 */

#include "slab_pool.h"

#include "esp_err.h"
#include "esp_log.h"

#include <string.h>

/**
 * @brief Slab Pool internal node structure
 */
typedef struct slab_node {
    slab_pool_t slab;       /**< Slab instance */
    struct slab_node *next; /**< Pointer to next node */
} slab_node_t;

/** Slab pool TAG name */
static const char *TAG = "SLAB_POOL";
/** Slab pool list */
static slab_node_t pool[SLAB_POOL_SIZE];
/** Slab pool list head */
static slab_node_t *free_list = NULL;

/** Slab pool list lock */
static SemaphoreHandle_t pool_lock = NULL;
/** Slab pool initialization state */
static bool is_slab_pool_initialized = false;

/**
 * @brief Lock pool
 */
static void pool_lock_take (void)
{
    (void) xSemaphoreTake (pool_lock, portMAX_DELAY);
}

/**
 * @brief Unlock pool
 */
static void pool_lock_give (void)
{
    (void) xSemaphoreGive (pool_lock);
}

esp_err_t slab_pool_init (void)
{
    esp_err_t err = ESP_OK;
    if ((is_slab_pool_initialized == true) || (pool_lock != NULL))
    {
        err = ESP_ERR_INVALID_STATE;
    }

    if (err == ESP_OK)
    {
        pool_lock = xSemaphoreCreateMutex ();
        if (pool_lock == NULL)
        {
            err = ESP_ERR_NO_MEM;
        }
    }

    if (err == ESP_OK)
    {
        // Initialize free list
        free_list = &pool[0];

        for (size_t idx = 0; idx < (SLAB_POOL_SIZE - 1); ++idx)
        {
            pool[idx].next = &pool[idx + 1U];
        }

        pool[SLAB_POOL_SIZE - 1U].next = NULL;
        is_slab_pool_initialized       = true;
        ESP_LOGI (TAG, "Initialized");
    }
    else
    {
        ESP_LOGW (TAG, "Failed to initialize: (%s).", esp_err_to_name (err));
    }

    return err;
}

slab_pool_t *slab_pool_alloc (void)
{
    slab_pool_t *slab = NULL;

    if (pool_lock != NULL)
    {

        pool_lock_take ();

        if (free_list != NULL)
        {
            slab_node_t *node = free_list;
            free_list         = node->next;

            slab = &node->slab;

            // Clear metadata
            slab->length = 0;
        }

        pool_lock_give ();
    }

    return slab;
}

void slab_pool_free (slab_pool_t *slab_ptr)
{
    if ((is_slab_pool_initialized == true) && (slab_ptr != NULL) && (pool_lock != NULL))
    {

        // Convert slab pointer back to node
        slab_node_t *node = (slab_node_t *) (((uint8_t *) slab_ptr) - offsetof (slab_node_t, slab));

        pool_lock_take ();

        node->next = free_list;
        free_list  = node;

        pool_lock_give ();
    }
}

void slab_pool_deinit (void)
{
    if (pool_lock != NULL)
    {
        vSemaphoreDelete (pool_lock);
        pool_lock = NULL;
    }

    free_list = NULL;
    (void) memset ((void *) pool, 0x00, sizeof (pool));
    is_slab_pool_initialized = false;
}
