/**
 * @file logger_service.c
 * @ingroup logger_module
 * @author Abanoub Salah
 * @brief Implementation of the logger service
 *
 * @details
 * - Uses blackbox logger to store and retrieve entries
 * - Enqueue log entry or store log directly
 * - Fetch next to get available next replay
 * - Uses a task to store enqueued entries
 */

#include "logger_service.h"

#include "debug_pins.h"
#include "esp_log.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "metrics.h"
#include "runtime_tasks.h"
#include "telemetry_pipeline.h"

#define LOGGER_SERVICE_STORE_QUOTA          (2U)  /**< Logger store quota */
#define LOGGER_SERVICE_LOGGING_QUEUE_LENGTH (32U) /**< Logger queue length */

/** Logger TAG name */
static const char *TAG = "LOGGER_SERVICE";
/** Logger task handle */
static TaskHandle_t logger_service_task_handle = NULL;
/** Logger queue handle */
static QueueHandle_t logging_queue = NULL;
/** Blackbox Logger instance */
static blackbox_logger_t logger = {0};

/**
 * @brief Task used to store enqueued entries
 *
 * @param parameters_void_ptr Void pointer to parameters
 */
static void logger_service_task (void *parameters_void_ptr)
{
    (void) parameters_void_ptr;
    uint32_t ulNotifiedValue = 0;
    bool work_pending        = false;

    ESP_LOGI (TAG, "Task \"%s\" started", pcTaskGetName (NULL));
    while (true)
    {
        TickType_t xTicksToWait = ((work_pending == true) ? 0 : portMAX_DELAY);
        xTaskNotifyWait (0x00, ULONG_MAX, &ulNotifiedValue, xTicksToWait);

        if ((ulNotifiedValue & LOGGER_SERVICE_TASK_NOTIFY_STOP_BIT) != 0)
        {
            ESP_LOGI (TAG, "Task \"%s\" stopped", pcTaskGetName (NULL));
            vTaskDelete (NULL);
        }

        telemetry_pipeline_record_t payload;
        uint32_t processed_logs = 0;
        while ((xQueueReceive (logging_queue, &payload, 0) == pdTRUE) &&
               (processed_logs < LOGGER_SERVICE_STORE_QUOTA))
        {
            DEBUG_GPIO_SET (DEBUG_PINS_FRAME_LOGGED);
            logger_service_store (&payload);
            ++processed_logs;
            DEBUG_GPIO_CLR (DEBUG_PINS_FRAME_LOGGED);
        }

        // Re-evaluate pending work for the next loop
        work_pending = (uxQueueMessagesWaiting (logging_queue) > 0);
    }
}

/**
 * @brief Stop logger service task
 */
static void logger_service_stop_task (void)
{
    if (logger_service_task_handle != NULL)
    {
        (void) xTaskNotify (logger_service_task_handle, LOGGER_SERVICE_TASK_NOTIFY_STOP_BIT, eSetBits);
    }
}

/** Logger task configuration structure */
runtime_task_config_t logger_service_task_config = {.name        = LOGGER_SERVICE_TASK_NAME,
                                                    .entry       = logger_service_task,
                                                    .arg         = NULL,
                                                    .stack_depth = LOGGER_SERVICE_TASK_STACK_DEPTH,
                                                    .priority    = LOGGER_SERVICE_TASK_PRIORITY,
                                                    .core_id     = LOGGER_SERVICE_TASK_CPU_AFFINITY,
                                                    .handle      = &logger_service_task_handle,
                                                    .stop_func   = logger_service_stop_task};

bool logger_service_init (blackbox_logger_config_t *blackbox_logger_config_ptr,
                          void (*on_replay_available_cb) (void))
{
    bool is_init = true;
    if ((blackbox_logger_config_ptr == NULL))
    {
        is_init = false;
    }

    if (is_init == true)
    {
        blackbox_logger_config_ptr->on_replay_available_func = on_replay_available_cb;
        is_init = (blackbox_logger_init (&logger, blackbox_logger_config_ptr) == BLACKBOX_LOGGER_OK);
    }

    if (is_init == true)
    {
        logging_queue =
            xQueueCreate (LOGGER_SERVICE_LOGGING_QUEUE_LENGTH, sizeof (telemetry_pipeline_record_t));
        is_init = (logging_queue != NULL);
    }

    return is_init;
}

bool logger_service_enqueue (const telemetry_pipeline_record_t *payload_ptr)
{
    bool is_enqueued = (xQueueSend (logging_queue, payload_ptr, 0) == pdTRUE);

    if (is_enqueued == true)
    {
        xTaskNotify (logger_service_task_handle, LOGGER_SERVICE_TASK_NOTIFY_LOG_BIT, eSetBits);
    }

    return is_enqueued;
}

bool logger_service_store (const telemetry_pipeline_record_t *payload_ptr)
{
    bool is_stored                     = true;
    slab_pool_t *slab_ptr              = payload_ptr->slab_ptr;
    blackbox_logger_entry_view_t entry = {.data_ptr = slab_ptr->data, .length = slab_ptr->length};

    blackbox_logger_err_t err = blackbox_logger_write (&logger, &entry);
    if (err != BLACKBOX_LOGGER_OK)
    {
        event_bus_t event = {.type         = EVENT_BUS_EVENT_ERROR,
                             .payload.data = METRICS_STAT_LOGGER_WRITE_ERRORS,
                             .size         = sizeof (metrics_stat_t),
                             .timestamp_us = payload_ptr->timestamp_us};
        (void) event_bus_publish (&event);
        is_stored = false;
    }
    slab_pool_free (slab_ptr);

    return is_stored;
}

bool logger_service_has_backlog (void)
{
    return blackbox_logger_has_replay_data (&logger);
}

bool logger_service_fetch_next_replay (telemetry_pipeline_record_t *payload_ptr, blackbox_logger_iter_cb_t cb)
{
    bool is_ready = true;

    payload_ptr->slab_ptr = slab_pool_alloc ();

    is_ready = (payload_ptr->slab_ptr != NULL);
    if (is_ready == true)
    {
        blackbox_logger_entry_view_t entry_ptr = {.data_ptr = payload_ptr->slab_ptr->data, .length = 0};

        is_ready = (blackbox_logger_next_replay (&logger, cb, payload_ptr, &entry_ptr,
                                                 SLAB_POOL_MAX_DATA_SIZE) == BLACKBOX_LOGGER_OK);
        if (is_ready != true)
        {
        }
        slab_pool_free (payload_ptr->slab_ptr);
    }

    return is_ready;
}

void logger_service_deinit (void)
{
    blackbox_logger_deinit (&logger);

    if (logging_queue != NULL)
    {
        vQueueDelete (logging_queue);
        logging_queue = NULL;
    }
}
