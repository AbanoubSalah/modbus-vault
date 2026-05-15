/**
 * @file logger_service.h
 * @ingroup logger_module
 * @author Abanoub Salah
 * @brief Logger service provider
 *
 * @details
 * - Provided services
 *     - Logging entry enqueue
 *     - Logging entry store
 *     - Inquire about replay backlog
 *     - Fetch next replay
 *     - Provide a task for storing enqueued entries
 */

#ifndef LOGGER_SERVICE_H
#define LOGGER_SERVICE_H

#include "blackbox_logger.h"
#include "freertos/FreeRTOS.h"
#include "telemetry_pipeline.h"

#define LOGGER_SERVICE_TASK_NAME            ("LOG_SRV") /**< Task name */
#define LOGGER_SERVICE_TASK_STACK_DEPTH     (3072U)     /**< Stack depth */
#define LOGGER_SERVICE_TASK_PRIORITY        (5U)        /**< Priority */
#define LOGGER_SERVICE_TASK_CPU_AFFINITY    (1U)        /**< CPU affinity */
#define LOGGER_SERVICE_TASK_NOTIFY_STOP_BIT (1U << 0)   /**< Task stop bit */
#define LOGGER_SERVICE_TASK_NOTIFY_LOG_BIT  (1U << 1)   /**< Task notify for log bit */

/**
 * @brief Initialize logger service
 *
 * @details Initialize blackbox logger instance used in different
 * functionality throughout
 *
 * @param blackbox_logger_config_ptr Pointer to Blackbox logger configuration structure
 * @param on_replay_available_cb Callback for when replay available
 *
 * @return true on initialize success false otherwise
 */
bool logger_service_init (blackbox_logger_config_t *blackbox_logger_config_ptr,
                          void (*on_replay_available_cb) (void));

/**
 * @brief Enqueue entries for storing
 *
 * @param payload_ptr Pointer to payload
 *
 * @return true if enqueued false otherwise
 */
bool logger_service_enqueue (const telemetry_pipeline_record_t *payload_ptr);

/**
 * @brief Store an entry
 *
 * @param payload_ptr Pointer to payload
 *
 * @return true if stored false otherwise
 */
bool logger_service_store (const telemetry_pipeline_record_t *payload_ptr);

/**
 * @brief Logger has a backlog
 *
 * @return true if logger has a backlog false otherwise
 */
bool logger_service_has_backlog (void);

/**
 * @brief Fetch next replay
 *
 * @param payload_ptr Pointer to payload
 * @param cb callback
 *
 * @return true if fetched then processed by callback false otherwise
 */
bool logger_service_fetch_next_replay (telemetry_pipeline_record_t *payload_ptr,
                                       blackbox_logger_iter_cb_t cb);

/**
 * @brief Deinitialize logger service
 *
 * @details Deinitialize blackbox logger and delete used queue
 */
void logger_service_deinit (void);

#endif
