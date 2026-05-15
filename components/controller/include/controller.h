/**
 * @file controller.h
 * @ingroup controller_module
 * @author Abanoub Salah
 * @brief System orchestration layer
 *
 * @details
 * - Responsibilities
 *     - Trigger system initialization/deinitialization
 *     - Trigger tasks start/stop
 */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "esp_err.h"

/**
 * @brief System states structure
 */
typedef enum {
    CONTROLLER_STATE_UNINITIALIZED, /**< Uninitialized: Unknown state */
    CONTROLLER_STATE_INIT,          /**< Transitioning: Setting up hardware/drivers */
    CONTROLLER_STATE_RUNNING,       /**< Nominal: System operational */
    CONTROLLER_STATE_FAULT,         /**< Critical error encountered */
    CONTROLLER_STATE_STOPPING,      /**< Safe shutdown/reboot process */
} controller_state_t;

/**
 * @brief Start controller
 *
 * @details Initialize, setup and start different system
 * components
 */
void controller_run (void);

/**
 * @brief Get current controller state
 *
 * @return Controller state
 */
controller_state_t controller_get_state (void);

/**
 * @brief Stop controller
 *
 * @details Request deinitialize and stopping of different
 * system components
 */
void controller_request_stop (void);

#endif
