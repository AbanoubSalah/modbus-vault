/**
 * @file controller.c
 * @ingroup controller_module
 * @author Abanoub Salah
 * @brief Implementation of the system orchestration layer
 *
 * @details
 * - Goes through different controller states throughout application life
 *     - Initialization: Initialize system and start tasks
 *     - Running: System is running
 *     - Fault: System is in critical error state
 *     - Stopping: System is stopping (reversing init step)
 * - Provides APIs to get/set current controller state
 */

#include "controller.h"

#include "esp_err.h"
#include "esp_timer.h"
#include "metrics.h"
#include "runtime_tasks.h"
#include "slab_pool.h"
#include "system_init.h"
#include "telemetry_service.h"

#define CONTROLLER_WAIT_ON_LOCK_TIME_US (100U)     /**< State lock wait timeout in micro seconds */
#define CONTROLLER_YIELD_TIME_US        (1000000U) /**< Yield time of control loop in micro seconds */

/** Current controller state */
static controller_state_t current_state = CONTROLLER_STATE_UNINITIALIZED;
/** State lock handle */
static SemaphoreHandle_t state_mutex = NULL;

/**
 * @brief Set controller state
 *
 * @param new_state Controller state
 */
static void set_state_helper (controller_state_t new_state)
{
    if ((state_mutex != NULL) &&
        (xSemaphoreTake (state_mutex, pdMS_TO_TICKS (CONTROLLER_WAIT_ON_LOCK_TIME_US))))
    {
        current_state = new_state;
        xSemaphoreGive (state_mutex);
    }
}

/**
 * @brief Handles system initialization
 *
 * @details
 * - Initialize different system components
 * - Start different system tasks
 *
 * @return esp_err_t Handle result
 * @retval ESP_OK Handle success
 * @retval any Any error from down layers propagates upward
 */
static esp_err_t handle_init_helper (void)
{
    esp_err_t err = system_init ();
    if (err == ESP_OK)
    {
        err = ((runtime_tasks_start_all () == pdPASS) ? ESP_OK : ESP_FAIL);
    }

    return err;
}

/**
 * @brief Handles system deinitialization
 *
 * @details
 * - Deinitialize different system components
 * - Stop different system tasks
 */
static void handle_deinit_helper (void)
{
    runtime_tasks_stop_all ();
    system_deinit ();
}

void controller_run (void)
{
    state_mutex = xSemaphoreCreateMutex ();

    if ((state_mutex != NULL))
    {
        set_state_helper (CONTROLLER_STATE_INIT);

        controller_state_t state_loop_value = controller_get_state ();
        while (state_loop_value != CONTROLLER_STATE_UNINITIALIZED)
        {
            state_loop_value = controller_get_state ();
            switch (state_loop_value)
            {
            case CONTROLLER_STATE_INIT:
                if (handle_init_helper () == ESP_OK)
                {
                    set_state_helper (CONTROLLER_STATE_RUNNING);
                }
                else
                {
                    set_state_helper (CONTROLLER_STATE_FAULT);
                }
                break;

            case CONTROLLER_STATE_RUNNING:
                break;

            case CONTROLLER_STATE_FAULT:
                // Attempt recovery
                set_state_helper (CONTROLLER_STATE_INIT);
                break;
            case CONTROLLER_STATE_STOPPING:
                handle_deinit_helper ();
                set_state_helper (CONTROLLER_STATE_UNINITIALIZED);
                break;
            default:
                break;
            }

            // Yield to other RTOS tasks
            vTaskDelay (pdMS_TO_TICKS (CONTROLLER_YIELD_TIME_US));
        }
        vSemaphoreDelete (state_mutex);
        state_mutex = NULL;
    }
}

controller_state_t controller_get_state (void)
{
    controller_state_t ret = CONTROLLER_STATE_UNINITIALIZED;
    if ((state_mutex != NULL) &&
        (xSemaphoreTake (state_mutex, pdMS_TO_TICKS (CONTROLLER_WAIT_ON_LOCK_TIME_US))))
    {
        ret = current_state;
        xSemaphoreGive (state_mutex);
    }

    return ret;
}

void controller_request_stop (void)
{
    set_state_helper (CONTROLLER_STATE_STOPPING);
}
