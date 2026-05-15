/**
 * @file system_init.h
 * @ingroup controller_module
 * @author Abanoub Salah
 *
 * @brief System initialization header
 *
 * @details Contains system initialization definitions for the project
 */

#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include "esp_err.h"

/**
 * @brief Initialization step enum
 */
typedef enum {
    INIT_STEP_DEFAULT_CONFIG, /** Default configuration step */
    INIT_STEP_NVS,            /**< Nvs step */
    INIT_STEP_CONFIG,         /**< Configuration step */
    INIT_STEP_TELE_PLIN,      /**< Telemetry pipeline step */
    INIT_STEP_TELE_SERV,      /**< Telemetry service step */
    INIT_STEP_LOG_SERV,       /**< Logger service step */
    INIT_STEP_WIFI,           /**< WiFi step */
    INIT_STEP_EVENT_BUS,      /**< Event bus step */
    INIT_STEP_METRICS,        /**< Metrics step */
    INIT_STEP_SLAB,           /**< Slab pool step */
    INIT_STEP_COMPLETE        /**< Initialization complete step */
} init_step_t;

/**
 * @brief Initialize the system
 *
 * @details Initialize the system by following a preset order
 * as defined inside the function state machine.
 * If not succeeded calls deinit and returns error
 * code
 *
 * @return esp_err_t Initialize result
 * @retval ESP_OK on Initialize success
 * @retval any Any error from down layers propagates upward
 */
esp_err_t system_init (void);

/**
 * @brief Deinitialize the system
 *
 * @details Deinitialize the system by calling all deinit functions
 * for all previously initialized or not components of the system
 *
 * @note Assumes called deinit functions handles none-initialized state
 * of the component
 */
void system_deinit (void);

#endif
