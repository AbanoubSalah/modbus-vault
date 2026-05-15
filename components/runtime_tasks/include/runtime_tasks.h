/**
 * @file runtime_tasks.h
 * @ingroup controller_module
 * @author Abanoub Salah
 * @brief Provide system tasks with centralized start/stop facilities
 *
 * @details
 * - Register a task by using runtime task configurations structure
 * and declare it in runtime_task source file as external and add it
 * to tasks list
 * - Each registered task may include a stop function for it's own task
 * otherwise vTaskDelete will be used if handle is available
 */

#ifndef RUNTIME_TASKS_H
#define RUNTIME_TASKS_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/**
 * @brief Runtime task configuration structure
 *
 */
typedef struct {
    TaskFunction_t entry;     /**< Task code entry */
    const char *name;         /**< Task name */
    uint32_t stack_depth;     /**< Stack size */
    void *arg;                /**< Task argument */
    UBaseType_t priority;     /**< Task priority */
    TaskHandle_t *handle;     /**< Pointer to task handle */
    BaseType_t core_id;       /**< Task core id */
    void (*stop_func) (void); /**< Task stop function */
} runtime_task_config_t;

/**
 * @brief Start registered tasks
 *
 * @return BaseType_t pdPASS on success pdFAIL otherwise
 */
BaseType_t runtime_tasks_start_all (void);

/**
 * @brief Stop running registered tasks
 *
 */
void runtime_tasks_stop_all (void);

#endif
