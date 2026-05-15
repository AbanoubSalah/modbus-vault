/**
 * @file event_bus.c
 * @ingroup utilities_module
 * @author Abanoub Salah
 * @brief Implementation of the event bus
 *
 * @details
 * - Uses queues for publishers
 * - Starts a task for dispatching events to subscribers
 * - Subscribers can subscribe if slots are available which are limited
 * - Subscribers can unsubscribe using their slot ID provided during subscribing
 */

#include "event_bus.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "runtime_tasks.h"
#include "string.h"

#define EVENT_BUS_MAX_SUBSCRIBERS         (8U) /**< Maximum possible subscribers */
#define EVENT_BUS_COUNT_BACKOFF_WATERMARK (5U) /**< Task backoff when queue reach watermark */

/**
 * @brief Subscriber structure
 */
typedef struct {
    event_bus_event_t type; /**< Event subscribed to */
    event_bus_cb_t cb;      /**< Callback when event arrive */
    void *ctx;              /**< Context needed by subscriber */
} subscriber_t;

/** Event bus TAG name */
static const char *TAG = "EVENT_BUS";
/** Event bus task handle */
static TaskHandle_t event_bus_task_handle = NULL;
/** Event bus queue handle */
static QueueHandle_t event_queue;
/** Event bus subscribers list */
static subscriber_t subscribers[EVENT_BUS_MAX_SUBSCRIBERS];

/**
 * @brief Event bus task
 *
 * @details Loops over subscribers calling them if their event match
 * current one
 *
 * @param parameters_void_ptr Void pointer to task parameters
 */
static void event_bus_task (void *parameters_void_ptr)
{
    (void) parameters_void_ptr;
    event_bus_t event;
    uint32_t ulNotifiedValue = 0;

    ESP_LOGI (TAG, "Task \"%s\" started", pcTaskGetName (NULL));
    while (true)
    {
        if (xTaskNotifyWait (0x00, ULONG_MAX, &ulNotifiedValue, portMAX_DELAY) == pdPASS)
        {
            if ((ulNotifiedValue & EVENT_BUS_TASK_NOTIFY_STOP_BIT) != 0)
            {
                ESP_LOGI (TAG, "Task \"%s\" stopped", pcTaskGetName (NULL));
                event_bus_task_handle = NULL;
                vTaskDelete (NULL);
            }
        }

        while (xQueueReceive (event_queue, &event, 0) == pdPASS)
        {
            for (int8_t subscriber_idx = 0; subscriber_idx < (int8_t) EVENT_BUS_MAX_SUBSCRIBERS;
                 ++subscriber_idx)
            {
                if ((subscribers[subscriber_idx].cb != NULL) &&
                    (subscribers[subscriber_idx].type == event.type))
                {
                    subscribers[subscriber_idx].cb (&event, subscribers[subscriber_idx].ctx);
                }
            }
        }

        if (uxQueueMessagesWaiting (event_queue) > EVENT_BUS_COUNT_BACKOFF_WATERMARK)
        {
            vTaskDelay (1);
        }
    }
}

/**
 * @brief Stop event bus task
 */
static void event_bus_stop_task (void)
{
    if (event_bus_task_handle != NULL)
    {
        (void) xTaskNotify (event_bus_task_handle, EVENT_BUS_TASK_NOTIFY_STOP_BIT, eSetBits);
    }
}

/** Event bus task configuration structure */
runtime_task_config_t event_bus_task_config = {.name        = EVENT_BUS_TASK_NAME,
                                               .entry       = event_bus_task,
                                               .arg         = NULL,
                                               .stack_depth = EVENT_BUS_TASK_STACK_DEPTH,
                                               .priority    = EVENT_BUS_TASK_PRIORITY,
                                               .core_id     = EVENT_BUS_TASK_CPU_AFFINITY,
                                               .handle      = &event_bus_task_handle,
                                               .stop_func   = event_bus_stop_task};

esp_err_t event_bus_init (size_t queue_size)
{
    esp_err_t err = ESP_OK;
    if (queue_size == 0U)
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if (err == ESP_OK)
    {
        event_queue = xQueueCreate (queue_size, sizeof (event_bus_event_t));
        if (event_queue == NULL)
        {
            err = ESP_ERR_NO_MEM;
        }
    }

    if (err == ESP_OK)
    {
        (void) memset (subscribers, 0, sizeof (subscribers));
    }

    if (err != ESP_OK)
    {
        event_bus_deinit ();
        ESP_LOGW (TAG, "Failed to initialize: (%s).", esp_err_to_name (err));
    }
    else
    {
        ESP_LOGI (TAG, "Initialized");
    }

    return err;
}

bool event_bus_publish (const event_bus_t *event_ptr)
{
    bool res = true;
    if ((event_ptr == NULL) || (event_queue == NULL))
    {
        res = false;
    }

    if (res == true)
    {
        res = (xQueueSend (event_queue, event_ptr, 0) == pdPASS);
        if (res == pdPASS)
        {
            xTaskNotify (event_bus_task_handle, EVENT_BUS_TASK_NOTIFY_event_available_BIT, eSetBits);
        }
    }

    return res;
}

int8_t event_bus_subscribe (event_bus_event_t type, event_bus_cb_t cb, void *ctx)
{
    int8_t ret = -1;

    for (int8_t idx = 0; idx < (int8_t) EVENT_BUS_MAX_SUBSCRIBERS; ++idx)
    {
        if (subscribers[idx].cb == NULL)
        {
            subscribers[idx].type = type;
            subscribers[idx].cb   = cb;
            subscribers[idx].ctx  = ctx;
            ret                   = idx;
            break;
        }
    }

    return ret;
}

bool event_bus_unsubscribe (int8_t slot)
{
    bool ret = false;
    if ((slot >= 0) && (slot < (int8_t) EVENT_BUS_MAX_SUBSCRIBERS))
    {
        subscribers[slot].cb  = NULL;
        subscribers[slot].ctx = NULL;
        ret                   = true;
    }

    return ret;
}

void event_bus_deinit (void)
{
    if (event_queue != NULL)
    {
        (void) vQueueDelete (event_queue);
        event_queue = NULL;
    }

    (void) memset (subscribers, 0, sizeof (subscribers));
}
