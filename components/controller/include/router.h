/**
 * @file router.h
 * @ingroup controller_module
 * @author Abanoub Salah
 * @brief System routing
 *
 * @details Route serialized frames and replays
 */

#ifndef ROUTER_H
#define ROUTER_H

#include "telemetry_pipeline.h"

/**
 * @brief Dispatch serialized frames
 *
 * @details Dispatch serialized frames to be published if connected
 * or loggger otherwise
 *
 * @param payload_ptr Pointer to payload
 *
 * @note This needs to be as light as possible so it just enqueues to
 * corresponding queue
 */
void router_dispatch (const telemetry_pipeline_record_t *payload_ptr);

/**
 * @brief Try publishing replays
 *
 * @details Fetch next logged replay if MQTT connected and try to send it
 *
 * @return true on success false otherwise
 */
bool router_publish_replay (void);

/**
 * @brief Notify telemetry service that replay is available
 */
void router_on_replay_available_callback (void);

#endif
