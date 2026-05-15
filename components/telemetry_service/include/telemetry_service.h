/**
 * @file telemetry_service.h
 * @ingroup telemetry_module
 * @author Abanoub Salah
 * @brief Provide telemetry services
 *
 * @details
 * - Publish live/replay records using it's task
 * - Holds MQTT connectivity state
 */

#ifndef TELEMETRY_SERVICE_H
#define TELEMETRY_SERVICE_H

#include "freertos/FreeRTOS.h"
#include "mqtt_bridge.h"
#include "telemetry_pipeline.h"

#define TELEMETRY_SERVICE_TASK_NAME              ("TELE_SRVC") /**< Task name */
#define TELEMETRY_SERVICE_TASK_STACK_DEPTH       (4096U)       /**< Stack depth */
#define TELEMETRY_SERVICE_TASK_PRIORITY          (4U)          /**< Priority */
#define TELEMETRY_SERVICE_TASK_CPU_AFFINITY      (1U)          /**< CPU affinity */
#define TELEMETRY_SERVICE_TASK_NOTIFY_STOP_BIT   (1U << 0)     /**< Task stop bit */
#define TELEMETRY_SERVICE_TASK_NOTIFY_LIVE_BIT   (1U << 1)     /**< Task notify for live bit */
#define TELEMETRY_SERVICE_TASK_NOTIFY_REPLAY_BIT (1U << 2)     /**< Task notify for replay bit */
#define TELEMETRY_SERVICE_TASK_NOTIFY_ONLINE_BIT (1U << 3)     /**< Task notify for online bit */

/**
 * @brief Telemetry service_ topics enum
 *
 * @note Must be kept contiguous and its used in-order throughout code
 */
typedef enum {
    TELEMETRY_SERVICE_TOPICS_LIVE   = 0U, /**< Live data while connected */
    TELEMETRY_SERVICE_TOPICS_REPLAY = 1U, /**< Replay of stored data during disconnection */
    TELEMETRY_SERVICE_TOPICS_STATUS = 2U, /**< Status of system */
    TELEMETRY_SERVICE_TOPICS_MAX          /**< Topics count */
} telemetry_service_topics_t;

/**
 * @brief Initialize telemetry service
 *
 * @details Initialize MQTT bridge instance and create queue
 *
 * @param mqtt_bridge_config_ptr Pointer to MQTT bridge configuration structure
 * @param replay_check_cb Callback for replay check
 * @return true on Initialize success false otherwise
 */
bool telemetry_service_init (mqtt_bridge_config_t *mqtt_bridge_config_ptr, bool (*replay_check_cb) (void));

/**
 * @brief Enqueue live record
 *
 * @param payload_ptr Pointer to record
 * @return true on success false otherwise
 */
bool telemetry_service_enqueue_live (const telemetry_pipeline_record_t *payload_ptr);

/**
 * @brief Publish live record
 *
 * @param payload_ptr Pointer to record
 * @return true on success false otherwise
 */
bool telemetry_service_publish_live (const telemetry_pipeline_record_t *payload_ptr);

/**
 * @brief Publish replay record
 *
 * @param payload_ptr Pointer to record
 * @return true on success false otherwise
 */
bool telemetry_service_publish_replay (const telemetry_pipeline_record_t *payload_ptr);

/**
 * @brief Publish status
 *
 * @param payload_ptr Pointer to record
 * @return true on success false otherwise
 */
bool telemetry_service_publish_status (const telemetry_pipeline_record_t *payload_ptr);

/**
 * @brief Send signal replay available
 */
void telemetry_service_notify_replay_available (void);

/**
 * @brief Set online state
 *
 * @param online Online state
 */
void telemetry_service_set_online (bool online);

/**
 * @brief Get online state
 *
 * @return true on online false otherwise
 */
bool telemetry_service_is_online (void);

/**
 * @brief Deinitialize telemetry service
 */
void telemetry_service_deinit (void);

#endif
