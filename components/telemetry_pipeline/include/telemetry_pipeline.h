/**
 * @file telemetry_pipeline.h
 * @ingroup telemetry_module
 * @author Abanoub Salah
 * @brief Provide pipeline for telemetry
 *
 * @details Serialize raw frames
 */

#ifndef TELEMETRY_PIPELINE_H
#define TELEMETRY_PIPELINE_H

#include "modbus_analyzer.h"
#include "slab_pool.h"

#define TELEMETRY_PIPELINE_TASK_NAME             ("TELE_PLN") /**< Task name */
#define TELEMETRY_PIPELINE_TASK_STACK_DEPTH      (4096U)      /**< Stack depth */
#define TELEMETRY_PIPELINE_TASK_PRIORITY         (10U)        /**< Priority */
#define TELEMETRY_PIPELINE_TASK_CPU_AFFINITY     (0U)         /**< CPU affinity */
#define TELEMETRY_PIPELINE_TASK_NOTIFY_STOP_BIT  (1U << 0)    /**< Task stop bit */
#define TELEMETRY_PIPELINE_TASK_NOTIFY_FRAME_BIT (1U << 1)    /**< Task notify for frame bit */

/**
 * @brief Telemetry pipeline record structure
 */
typedef struct {
    slab_pool_t *slab_ptr; /**< Pointer to a slab */
    int64_t timestamp_us;  /**< Record timestamp */
} telemetry_pipeline_record_t;

/**
 * @brief Initiate telemetry pipeline
 *
 * @details Initiate a modbus analyzer instance and
 * create serialized queue
 *
 * @param analyzer_config_ptr Pointer to Modbus analyzer configuration structure
 * @param record_ready_cb Callback on record ready
 * @return true if initiate success false otherwise
 */
bool telemetry_pipeline_init (modbus_analyzer_config_t *analyzer_config_ptr,
                              void (*record_ready_cb) (const telemetry_pipeline_record_t *));

/**
 * @brief Enqueue raw frame
 *
 * @param raw_frame_ptr Pointer to raw frame
 */
void telemetry_pipeline_enqueue_raw_frame (const modbus_analyzer_frame_t *raw_frame_ptr);

/**
 * @brief Deinitialize telemetry pipeline
 */
void telemetry_pipeline_deinit (void);

#endif
