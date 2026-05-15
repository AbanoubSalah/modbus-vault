/**
 * @file modbus_analyzer.c
 * @ingroup modbus_analyzer_module
 * @brief Implementation of the modbus analyzer
 *
 * @details
 * - Provide a task that receive raw bytes from lower
 * layer and shape them into frames using 'modbus slicer'
 * - Uses 'system event bus' to emit data/errors for consumers
 */

#include "modbus_analyzer.h"

#include "debug_pins.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "metrics.h"
#include "modbus_parser.h"
#include "runtime_tasks.h"

#define MODBUS_ANALYZER_EVENTS_QUEUE_SIZE (20U) /**< Task queue size */

/** Modbus analyzer TAG name */
static const char *TAG = "MODBUS_ANALYZER";
/** Modbus analyzer task handle */
static TaskHandle_t modbus_analyzer_task_handle;

/**
 * @brief Analyzer task
 *
 * @details Receive bytes from RS485 driver, checks for RS485 driver errors
 * then feeds them to slicer if there was data
 *
 * @param parameters_void_ptr Void pointer to task parameters
 *
 * @note parameters_void_ptr must be a pointer to analyzer instance
 */
static void modbus_analyzer_task (void *parameters_void_ptr)
{
    if (parameters_void_ptr != NULL)
    {
        modbus_analyzer_t *analyzer_ptr = (modbus_analyzer_t *) parameters_void_ptr;
        uint32_t ulNotifiedValue        = 0;

        // Calculate 3.5 characters in ticks (ensure at least 1 tick)
        TickType_t xTicksToWait = pdMS_TO_TICKS (analyzer_ptr->slicer.t3_5_us / 1000U);
        if (xTicksToWait == 0)
        {
            xTicksToWait = 1;
        }

        ESP_LOGI (TAG, "Task \"%s\" started", pcTaskGetName (NULL));
        while (true)
        {
            if (xTaskNotifyWait (0, ULONG_MAX, &ulNotifiedValue, 0) == pdPASS)
            {
                if ((ulNotifiedValue & MODBUS_ANALYZER_TASK_NOTIFY_STOP_BIT) != 0)
                {
                    ESP_LOGI (TAG, "Task \"%s\" stopped", pcTaskGetName (NULL));
                    vTaskDelete (NULL);
                }
            }

            rs485_driver_event_t frame;
            // Sleep until a frame arrive OR 3.5 characters time elapsed
            if (xQueueReceive (analyzer_ptr->raw_events_queue, &frame, xTicksToWait) == pdTRUE)
            {
                DEBUG_GPIO_SET (DEBUG_PINS_FRAME_CAPTURE);
                if ((frame.flags & RS485_DRIVER_EVENT_FLAG_DATA) != 0)
                {
                    // Data present
                    modbus_slicer_feed (&analyzer_ptr->slicer, frame.data, frame.length, frame.timestamp_us);
                }

                if ((frame.flags & RS485_DRIVER_EVENT_FLAG_RX_TIMEOUT) != 0)
                {
                    // 3.5 characters threshold was triggered
                    modbus_slicer_timeout (&analyzer_ptr->slicer);
                }

                if ((frame.flags & RS485_DRIVER_EVENT_FLAG_OVERFLOW) != 0)
                {
                    event_bus_t event = {.type         = EVENT_BUS_EVENT_ERROR,
                                         .timestamp_us = frame.timestamp_us,
                                         .payload.data = METRICS_STAT_RS485_DRIVER_OVERFLOW_ERRORS,
                                         .size         = sizeof (metrics_stat_t)};
                    (void) event_bus_publish (&event);
                }

                if ((frame.flags & RS485_DRIVER_EVENT_FLAG_PARITY) != 0)
                {
                    event_bus_t event = {.type         = EVENT_BUS_EVENT_ERROR,
                                         .timestamp_us = frame.timestamp_us,
                                         .payload.data = METRICS_STAT_RS485_DRIVER_PARITY_ERRORS,
                                         .size         = sizeof (metrics_stat_t)};
                    (void) event_bus_publish (&event);
                }
            }
            else
            {
                // Queue timeout reached. This means no data arrived for 3.5 characters period
                modbus_slicer_check_timeout (&analyzer_ptr->slicer);
            }
        }
    }
}

/**
 * @brief Stop modbus analyzer task
 */
static void modbus_analyzer_stop_task (void)
{
    if (modbus_analyzer_task_handle != NULL)
    {
        (void) xTaskNotify (modbus_analyzer_task_handle, MODBUS_ANALYZER_TASK_NOTIFY_STOP_BIT, eSetBits);
    }
}

/**
 * @brief Callback on-frame-ready from slicer
 *
 * @details Emit system event with type depends whether there
 * was an error or data-frame
 *
 * @param frame_ptr Pointer to modbus frame
 * @param arg_void_ptr Void pointer to argument
 */
static void emit_modbus_frame_callback (const modbus_slicer_frame_t *frame_ptr, void *arg_void_ptr)
{
    if (frame_ptr != NULL)
    {
        event_bus_t event = {
            .type         = EVENT_BUS_EVENT_ERROR,
            .size         = sizeof (metrics_stat_t),
            .timestamp_us = frame_ptr->timestamp_us,
        };

        if (frame_ptr->error != MODBUS_SLICER_OK)
        {
            if (frame_ptr->error == MODBUS_SLICER_ERROR_OVERFLOW)
            {
                event.payload.data = METRICS_STAT_MODBUS_OVERFLOW_ERRORS;
                // Publish event
                (void) event_bus_publish (&event);
            }

            if (frame_ptr->error == MODBUS_SLICER_ERROR_NO_MEM)
            {
                event.payload.data = METRICS_STAT_MODBUS_NO_MEM_ERRORS;
                // Publish event
                (void) event_bus_publish (&event);
            }
        }
        else
        {
            // CRC check using parser function
            esp_err_t crc_ok =
                modbus_parser_check_crc ((uint8_t *) frame_ptr->slab_ptr->data, frame_ptr->slab_ptr->length);
            if (crc_ok == ESP_OK)
            {
                modbus_analyzer_t *analyzer_ptr = (modbus_analyzer_t *) arg_void_ptr;
                modbus_analyzer_frame_t frame   = {.slab_ptr     = frame_ptr->slab_ptr,
                                                   .timestamp_us = frame_ptr->timestamp_us};
                if (analyzer_ptr->config_ptr->on_frame_cb != NULL)
                {
                    analyzer_ptr->config_ptr->on_frame_cb (&frame);
                }
            }
            else
            {
                event.payload.data = METRICS_STAT_MODBUS_CRC_ERRORS;
                // Publish event
                (void) event_bus_publish (&event);
            }
        }
    }
    DEBUG_GPIO_CLR (DEBUG_PINS_FRAME_CAPTURE);
}

/**
 * @brief  Callback for rs485 driver to queue events
 *
 * @param arg_void_ptr Void pointer to modbus analyzer type structure
 * @param event_ptr Pointer to rs485 driver event
 */
static void on_rs485_driver_event_callback (void *arg_void_ptr, const rs485_driver_event_t *event_ptr)
{
    modbus_analyzer_t *analyzer_ptr = (modbus_analyzer_t *) arg_void_ptr;
    if ((analyzer_ptr != NULL) && (arg_void_ptr != NULL))
    {
        xQueueSend (analyzer_ptr->raw_events_queue, event_ptr, 0);
    }
}

/** Modbus analyzer task configuration structure */
runtime_task_config_t modbus_analyzer_task_config = {.name        = MODBUS_ANALYZER_TASK_NAME,
                                                     .entry       = modbus_analyzer_task,
                                                     .arg         = NULL,
                                                     .stack_depth = MODBUS_ANALYZER_TASK_STACK_DEPTH,
                                                     .priority    = MODBUS_ANALYZER_TASK_PRIORITY,
                                                     .core_id     = MODBUS_ANALYZER_TASK_CPU_AFFINITY,
                                                     .handle      = &modbus_analyzer_task_handle,
                                                     .stop_func   = modbus_analyzer_stop_task};

esp_err_t modbus_analyzer_init (modbus_analyzer_t *analyzer_ptr, modbus_analyzer_config_t *config_ptr)
{
    esp_err_t err = ESP_OK;
    if ((analyzer_ptr == NULL) || (config_ptr == NULL) || (config_ptr->on_frame_cb == NULL) ||
        (config_ptr->rs485_driver_config_ptr == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
        ESP_LOGW (TAG, "Failed to initialize: (%s).", esp_err_to_name (err));
    }

    if (err == ESP_OK)
    {
        analyzer_ptr->config_ptr = config_ptr;
        /* Initialize RS485 driver */
        config_ptr->rs485_driver_config_ptr->on_event_cb              = on_rs485_driver_event_callback;
        config_ptr->rs485_driver_config_ptr->on_event_cb_arg_void_ptr = analyzer_ptr;
        err = rs485_driver_init (&analyzer_ptr->rs485_drv, config_ptr->rs485_driver_config_ptr);
    }

    if (err == ESP_OK)
    {
        // Calculate bit length
        /* Initialize modbus slicer */
        analyzer_ptr->slicer_config =
            (modbus_slicer_config_t){.baudrate      = config_ptr->rs485_driver_config_ptr->baudrate,
                                     .bit_length    = rs485_driver_get_bits_count (&analyzer_ptr->rs485_drv),
                                     .on_frame_func = emit_modbus_frame_callback,
                                     .get_time_us_func = esp_timer_get_time,
                                     .cb_arg_void_ptr  = analyzer_ptr};

        modbus_slicer_init (&analyzer_ptr->slicer, &analyzer_ptr->slicer_config);
        modbus_analyzer_task_config.arg = analyzer_ptr;

        /* Create events queue */
        analyzer_ptr->raw_events_queue =
            xQueueCreate (MODBUS_ANALYZER_EVENTS_QUEUE_SIZE, sizeof (rs485_driver_event_t));
        if (analyzer_ptr->raw_events_queue == NULL)
        {
            err = ESP_ERR_NO_MEM;
        }
        else
        {
            ESP_LOGI (TAG, "Initialized");
        }
    }

    return err;
}

void modbus_analyzer_deinit (modbus_analyzer_t *analyzer_ptr)
{
    if (analyzer_ptr->raw_events_queue != NULL)
    {
        vQueueDelete (analyzer_ptr->raw_events_queue);
        analyzer_ptr->raw_events_queue = NULL;
    }
    (void) rs485_driver_deinit (&analyzer_ptr->rs485_drv);
}
