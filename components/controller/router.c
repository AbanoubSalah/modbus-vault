/**
 * @file router.c
 * @ingroup controller_module
 * @author Abanoub Salah
 * @brief Implementation of the system routing helpers
 *
 * @details
 * - Dispatch serialized frames publish/log
 * - Notify telemetry service when replay available through callback
 * - Try publishing available replays when connection resumes
 */

#include "router.h"

#include "blackbox_logger.h"
#include "logger_service.h"
#include "slab_pool.h"
#include "telemetry_service.h"

/**
 * @brief Publish replay and return error code
 *
 * @param arg_void_ptr Void pointer to argument
 * @param entry_ptr Pointer to log entry
 * @return true on success false otherwise
 */
static bool replay_callback (void *arg_void_ptr, const blackbox_logger_entry_view_t *entry_ptr)
{
    telemetry_pipeline_record_t *payload_ptr = (telemetry_pipeline_record_t *) arg_void_ptr;
    payload_ptr->slab_ptr->length            = entry_ptr->length;
    bool is_sent                             = telemetry_service_publish_replay (payload_ptr);

    return is_sent;
}

void router_dispatch (const telemetry_pipeline_record_t *payload_ptr)
{
    if (telemetry_service_is_online () == true)
    {
        (void) telemetry_service_enqueue_live (payload_ptr);
    }
    else
    {
        (void) logger_service_enqueue (payload_ptr);
    }
}

bool router_publish_replay (void)
{
    telemetry_pipeline_record_t payload;

    // Get next and process it with callback and let callback handle
    // logger acknowledgement
    bool is_replayed =
        logger_service_fetch_next_replay (&payload, (blackbox_logger_iter_cb_t) replay_callback);

    return is_replayed;
}

void router_on_replay_available_callback (void)
{
    telemetry_service_notify_replay_available ();
}
