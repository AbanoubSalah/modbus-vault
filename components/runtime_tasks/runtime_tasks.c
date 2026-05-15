/**
 * @file runtime_tasks.c
 * @ingroup controller_module
 * @author Abanoub Salah
 * @brief Implementation of the runtime tasks
 *
 * @details
 * - Registered tasks are externally defined to be intensionally added here
 * - Loops over registered tasks starting each one
 * - Loops over registered tasks to stopping each one starting from the last one
 */

#include "runtime_tasks.h"

#include "esp_log.h"
#include "slab_pool.h"
#include "telemetry_pipeline.h"

/** Runtime tasks TAG name */
static const char *TAG = "RUTIM_TASKS";

extern const runtime_task_config_t
    telemetry_pipeline_task_config; /**< Telemetry Pipeline task configuration structure */
extern const runtime_task_config_t
    telemetry_service_task_config; /**< Telemetry Service task configuration structure */
extern const runtime_task_config_t
    logger_service_task_config; /**< Telemetry Pipeline task configuration structure */
extern const runtime_task_config_t
    modbus_analyzer_task_config; /**< Modbus Aanalyzer task configuration structure */
extern const runtime_task_config_t event_bus_task_config;    /**< Event Bus task configuration structure */
extern const runtime_task_config_t rs485_driver_task_config; /**< RS485 driver task configuration structure */

/** List of registered tasks */
static const runtime_task_config_t *registered_tasks[] = {
    &event_bus_task_config,      &telemetry_pipeline_task_config, &telemetry_service_task_config,
    &logger_service_task_config, &modbus_analyzer_task_config,    &rs485_driver_task_config};
/** Registered tasks count */
static const int8_t REGISTERED_TASKS_COUNT = sizeof (registered_tasks) / sizeof (registered_tasks[0]);
/** Holds last started task */
static int8_t last_started_task_idx = -1;

BaseType_t runtime_tasks_start_all (void)
{
    BaseType_t err = pdPASS;

    while ((err == pdPASS) && (last_started_task_idx < (REGISTERED_TASKS_COUNT - 1)))
    {
        if (registered_tasks[last_started_task_idx + 1]->entry != NULL)
        {
            err = xTaskCreatePinnedToCore (registered_tasks[last_started_task_idx + 1]->entry,
                                           registered_tasks[last_started_task_idx + 1]->name,
                                           registered_tasks[last_started_task_idx + 1]->stack_depth,
                                           registered_tasks[last_started_task_idx + 1]->arg,
                                           registered_tasks[last_started_task_idx + 1]->priority,
                                           registered_tasks[last_started_task_idx + 1]->handle,
                                           registered_tasks[last_started_task_idx + 1]->core_id);
            if (err != pdPASS)
            {
                ESP_LOGW (TAG, "Couldn't create: %s", registered_tasks[last_started_task_idx + 1]->name);
                runtime_tasks_stop_all ();
            }
            else
            {
                ++last_started_task_idx;
            }
        }
    }

    return err;
}

void runtime_tasks_stop_all (void)
{
    for (; last_started_task_idx >= 0; --last_started_task_idx)
    {
        if (registered_tasks[last_started_task_idx + 1]->stop_func != NULL)
        {
            registered_tasks[last_started_task_idx + 1]->stop_func ();
        }
        else if (registered_tasks[last_started_task_idx + 1]->handle != NULL)
        {
            vTaskDelete (*(registered_tasks[last_started_task_idx + 1]->handle));
        }
        else
        {
        }
    }
}
