/**
 * @file modbus_analyzer.h
 * @ingroup modbus_analyzer_module
 * @author Abanoub Salah
 * @brief Analyzer for Modbus
 *
 * @details
 * - Provides interface for Modbus component
 *     - Receive bytes from lower layer and check for errors
 *     - Actively parse received bytes
 *     - Emits frame/error after parsing provided bytes
 */

#ifndef MODBUS_ANALYZER_H
#define MODBUS_ANALYZER_H

#include "esp_err.h"
#include "modbus_slicer.h"
#include "rs485_driver.h"
#include "slab_pool.h"

#include <stdint.h>

#define MODBUS_ANALYZER_TASK_NAME            ("MBUS_ALZ") /**< Task name */
#define MODBUS_ANALYZER_TASK_STACK_DEPTH     (3072U)      /**< Stack depth */
#define MODBUS_ANALYZER_TASK_PRIORITY        (15U)        /**< Priority */
#define MODBUS_ANALYZER_TASK_CPU_AFFINITY    (0U)         /**< CPU affinity */
#define MODBUS_ANALYZER_TASK_NOTIFY_STOP_BIT (1U << 0)    /**< Task stop bit */

/**
 * @brief Modbus analyzer frame structure
 */
typedef struct {
    slab_pool_t *slab_ptr; /**< Pointer to slab buffer */
    int64_t timestamp_us;  /**< Frame timestamp */
} modbus_analyzer_frame_t;

/**
 * @brief Modbus analyzer configuration structure
 *
 */
typedef struct {
    rs485_driver_config_t *rs485_driver_config_ptr;        /**< Pointer to RS485 driver configuration */
    void (*on_frame_cb) (const modbus_analyzer_frame_t *); /**< Callback on frame/error */
} modbus_analyzer_config_t;

/**
 * @brief Modbus analyzer structure
 */
typedef struct {
    modbus_analyzer_config_t *config_ptr; /**< Pointer to modbus analyzer configuration structure */
    rs485_driver_t rs485_drv;             /**< RS driver instance */
    modbus_slicer_t slicer;               /**< Slicer instance */
    modbus_slicer_config_t slicer_config; /**< Slicer configuration instance */
    QueueHandle_t raw_events_queue;       /**< Raw events queue handle */
} modbus_analyzer_t;

/**
 * @brief Initialize Modbus analyzer
 *
 * @details Initialize Modbus analyzer by initializing a slicer
 * instance
 *
 * @param analyzer_ptr Pointer to analyzer instance
 * @param config_ptr Pointer to analyzer configuration instance
 *
 * @return esp_err_t Initialize result
 * @retval ESP_OK Initialize success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 */
esp_err_t modbus_analyzer_init (modbus_analyzer_t *analyzer_ptr, modbus_analyzer_config_t *config_ptr);

/**
 * @brief Deinitialize Modbus analyzer
 *
 * @param analyzer_ptr Pointer to analyzer instance
 */
void modbus_analyzer_deinit (modbus_analyzer_t *analyzer_ptr);

#endif
