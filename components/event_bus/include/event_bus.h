/**
 * @file event_bus.h
 * @ingroup utilities_module
 * @author Abanoub Salah
 * @brief Event bus component
 *
 * @details
 * - Provides lightweight event bus to
 *     - Publish different system events
 *     - Decouple components
 *     - Provide subscribers access to event
 */

#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define EVENT_BUS_TASK_NAME                       ("EVENT_BUS") /**< Task name */
#define EVENT_BUS_TASK_STACK_DEPTH                (2024U)       /**< Stack depth */
#define EVENT_BUS_TASK_PRIORITY                   (3U)          /**< Priority */
#define EVENT_BUS_TASK_CPU_AFFINITY               (0U)          /**< CPU affinity */
#define EVENT_BUS_TASK_NOTIFY_STOP_BIT            (1U << 0U)    /**< Task stop bit */
#define EVENT_BUS_TASK_NOTIFY_event_available_BIT (1U << 1U)    /**< Task notify for event bit */

/**
 * @brief Event bus event type enum
 */
typedef enum {
    EVENT_BUS_EVENT_ERROR, /**< Error event */
} event_bus_event_t;

/**
 * @brief Event bus type structure
 */
typedef struct {
    event_bus_event_t type; /**< Event type */
    union {
        uint32_t data;    /**< Used when passing a value */
        void *data_ptr;   /**< Used when passing a pointer */
    } payload;            /**< Event payload */
    size_t size;          /**< Size of data */
    int64_t timestamp_us; /**< Event time in uSeconds */
} event_bus_t;

typedef void (*event_bus_cb_t) (const event_bus_t *event, void *ctx); /**< Callback for when event trigger */

/**
 * @brief Initialize event bus
 *
 * @param queue_size Size of the event queue
 *
 * @return esp_err_t Initialize result
 * @retval ESP_OK Initialize success
 * @retval BLACKBOX_FAIL Failed to create event task
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_NO_MEM No available memory for resource allocation
 */
esp_err_t event_bus_init (size_t queue_size);

/**
 * @brief Publish an event
 *
 * @details Publish an event by queue it to the event bus queue
 *
 * @param event_ptr Pointer to an event
 *
 * @return bool true on publish false otherwise
 */
bool event_bus_publish (const event_bus_t *event_ptr);

/**
 * @brief Subscribe to event
 *
 * @param type Event type
 * @param cb Callback for when event happens
 * @param ctx Context for the callback
 *
 * @return int8_t Subscription slot number and '-1' On fail
 */
int8_t event_bus_subscribe (event_bus_event_t type, event_bus_cb_t cb, void *ctx);

/**
 * @brief Unsubscribe from event
 *
 * @param slot Subscription slot number
 *
 * @return true on unsubscription false otherwise
 */
bool event_bus_unsubscribe (int8_t slot);

/**
 * @brief deinitialize event bus
 */
void event_bus_deinit (void);

#endif
