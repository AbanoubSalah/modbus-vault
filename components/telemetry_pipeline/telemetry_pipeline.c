/**
 * @file telemetry_pipeline.c
 * @ingroup telemetry_module
 * @author Abanoub Salah
 * @brief Implementation of the telemetry pipeline
 *
 * @details
 * - Initialize modbus analyzer instance
 * - On ready raw frame modbus analyzer enqueue frame
 * - Telemetry pipeline task continuously serialize raw frames queue
 * and calls registered callback function
 */

#include "telemetry_pipeline.h"

#include "debug_pins.h"
#include "esp_log.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "runtime_tasks.h"
#include "serializer.h"

/** Frame queue length */
#define TELEMETRY_PIPELINE_RAW_FRAME_QUEUE_LENGTH (32U)

/** Telemetry Pipeline TAG name */
static const char *TAG = "TELEMETRY_PIPELINE";
/** Telemetry Pipeline task handle */
static TaskHandle_t telemetry_pipeline_task_handle = NULL;
/** Telemetry Pipeline queue handle */
static QueueHandle_t raw_frame_queue = NULL;
/** On record ready callback function */
void (*record_ready_callback) (const telemetry_pipeline_record_t *) = NULL;
/** Modbus Analyzer instance*/
static modbus_analyzer_t analyzer;

/**
 * @brief Telemetry pipeline task serialize raw frames
 *
 * @details Telemetry pipeline task serialize raw frames and calls
 * registered callback function
 *
 * @param parameters_void_ptr
 */
static void telemetry_pipeline_task (void *parameters_void_ptr)
{
    (void) parameters_void_ptr;
    uint32_t ulNotifiedValue = 0;
    bool work_pending        = true;

    ESP_LOGI (TAG, "Task \"%s\" started", pcTaskGetName (NULL));
    while (true)
    {
        TickType_t xTicksToWait = ((work_pending == true) ? 0 : portMAX_DELAY);
        xTaskNotifyWait (0x00, ULONG_MAX, &ulNotifiedValue, xTicksToWait);

        if ((ulNotifiedValue & TELEMETRY_PIPELINE_TASK_NOTIFY_STOP_BIT) != 0)
        {
            ESP_LOGI (TAG, "Task \"%s\" stopped", pcTaskGetName (NULL));
            vTaskDelete (NULL);
        }

        modbus_analyzer_frame_t raw_frame;
        if (xQueueReceive (raw_frame_queue, &raw_frame, 0) == pdTRUE)
        {
            DEBUG_GPIO_SET (DEBUG_PINS_FRAME_SERIALIZED);
            slab_pool_t *slab_ptr = slab_pool_alloc ();
            if (slab_ptr != NULL)
            {
                esp_err_t err = serializer_pack (raw_frame.slab_ptr, raw_frame.timestamp_us, slab_ptr);
                if ((err == ESP_OK) && (record_ready_callback != NULL))
                {
                    telemetry_pipeline_record_t serialized_frame = {.slab_ptr     = slab_ptr,
                                                                    .timestamp_us = raw_frame.timestamp_us};
                    record_ready_callback (&serialized_frame);
                }
                else
                {
                    slab_pool_free (slab_ptr);
                }
            }
            slab_pool_free (raw_frame.slab_ptr);
            DEBUG_GPIO_CLR (DEBUG_PINS_FRAME_SERIALIZED);
        }

        work_pending = (uxQueueMessagesWaiting (raw_frame_queue) > 0);
    }
}

/**
 * @brief Stop telemetry pipeline task
 */
static void telemetry_pipeline_stop_task (void)
{
    if (telemetry_pipeline_task_handle != NULL)
    {
        (void) xTaskNotify (telemetry_pipeline_task_handle, TELEMETRY_PIPELINE_TASK_NOTIFY_STOP_BIT,
                            eSetBits);
    }
}

/** Telemetry Pipeline task configuration structure */
runtime_task_config_t telemetry_pipeline_task_config = {.name        = TELEMETRY_PIPELINE_TASK_NAME,
                                                        .entry       = telemetry_pipeline_task,
                                                        .arg         = NULL,
                                                        .stack_depth = TELEMETRY_PIPELINE_TASK_STACK_DEPTH,
                                                        .priority    = TELEMETRY_PIPELINE_TASK_PRIORITY,
                                                        .core_id     = TELEMETRY_PIPELINE_TASK_CPU_AFFINITY,
                                                        .handle      = &telemetry_pipeline_task_handle,
                                                        .stop_func   = telemetry_pipeline_stop_task};

bool telemetry_pipeline_init (modbus_analyzer_config_t *analyzer_config_ptr,
                              void (*record_ready_cb) (const telemetry_pipeline_record_t *))
{
    bool is_init = true;

    if (record_ready_cb == NULL)
    {
        is_init = false;
    }

    if (is_init == true)
    {
        analyzer_config_ptr->on_frame_cb = telemetry_pipeline_enqueue_raw_frame;
        is_init                          = (modbus_analyzer_init (&analyzer, analyzer_config_ptr) == ESP_OK);
    }

    if (is_init == true)
    {
        record_ready_callback = record_ready_cb;
        raw_frame_queue =
            xQueueCreate (TELEMETRY_PIPELINE_RAW_FRAME_QUEUE_LENGTH, sizeof (modbus_analyzer_frame_t));
        is_init = (raw_frame_queue != NULL);
    }

    return is_init;
}

void telemetry_pipeline_enqueue_raw_frame (const modbus_analyzer_frame_t *raw_frame_ptr)
{
    if ((raw_frame_ptr != NULL) && (xQueueSend (raw_frame_queue, raw_frame_ptr, 0) == pdTRUE))
    {
        xTaskNotify (telemetry_pipeline_task_handle, TELEMETRY_PIPELINE_TASK_NOTIFY_FRAME_BIT, eSetBits);
    }
}

void telemetry_pipeline_deinit (void)
{
    if (raw_frame_queue != NULL)
    {
        vQueueDelete (raw_frame_queue);
    }

    modbus_analyzer_deinit (&analyzer);
}
