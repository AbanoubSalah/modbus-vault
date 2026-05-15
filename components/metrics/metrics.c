/**
 * @file metrics.c
 * @ingroup utilities_module
 * @brief Implementation of the metrics
 *
 * @details
 * - Uses 'system event bus' module to receive stats events
 * - Can return a coy of stats structure or print them through
 * ESP logging facilities
 */

#include "metrics.h"

#include "esp_log.h"
#include "event_bus.h"
#include "slab_pool.h"

/** Metrics TAG name */
static const char *TAG = "METRICS";
/** Metrics counters structure */
static uint32_t system_metrics[METRICS_STAT_MAX] = {0};

/**
 * @brief Increment a metric counter
 *
 * @param stat Stat to increment
 */
static void metrics_increment_stat_helper (metrics_stat_t stat)
{
    if (stat < METRICS_STAT_MAX)
    {
        __sync_fetch_and_add (&system_metrics[stat], 1);
    }
}

/**
 * @brief Callback on error
 *
 * @details Increment received event corresponding stat
 *
 * @param event_ptr Pointer to event
 * @param ctx Pointer to event context
 */
static void on_error_callback (const event_bus_t *event_ptr, void *ctx)
{
    (void) ctx;
    metrics_increment_stat_helper ((metrics_stat_t) event_ptr->payload.data);
}

esp_err_t metrics_init (void)
{
    esp_err_t err = ESP_OK;
    metrics_reset_all ();

    int8_t subscription = event_bus_subscribe (EVENT_BUS_EVENT_ERROR, on_error_callback, NULL);
    if (subscription < 0)
    {
        err = ESP_ERR_NO_MEM;
        ESP_LOGW (TAG, "Failed to initialize: (%s).", esp_err_to_name (err));
    }
    else
    {
        ESP_LOGI (TAG, "Initialized");
    }

    return err;
}

esp_err_t metrics_get_snapshot (uint32_t *buf_ptr, size_t buf_length, uint16_t *copied_length)
{
    esp_err_t err              = ESP_OK;
    size_t system_metrics_size = (METRICS_STAT_MAX * sizeof (system_metrics[0]));

    if ((buf_ptr == NULL) || (copied_length == NULL) || (buf_length < system_metrics_size))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    else
    {
        for (size_t idx = 0; idx < METRICS_STAT_MAX; ++idx)
        {
            buf_ptr[idx] = __sync_val_compare_and_swap (&system_metrics[idx], 0, 0);
        }
        *copied_length = (uint16_t) system_metrics_size;
    }

    return err;
}

void metrics_log_all (void)
{
    uint32_t snapshot[METRICS_STAT_MAX];
    uint16_t ret_length;
    metrics_get_snapshot (snapshot, sizeof (snapshot), &ret_length);

    for (size_t idx = 0; idx < METRICS_STAT_MAX; ++idx)
    {
        ESP_LOGI (TAG, "%s = %lu", metrics_stat_to_string (idx), (unsigned long) snapshot[idx]);
    }
}

void metrics_reset_all (void)
{
    for (size_t idx = 0; idx < METRICS_STAT_MAX; ++idx)
    {
        __sync_fetch_and_and (&system_metrics[idx], 0);
    }
}

/** Extract stat name from table */
#define AS_STRING(id, str) str,
const char *metrics_stat_to_string (metrics_stat_t stat)
{
    /** Stat names list */
    static const char *const stat_names[] = {METRICS_TABLE (AS_STRING)};

    const char *res = "UNKNOWN";

    if (stat < METRICS_STAT_MAX)
    {
        res = stat_names[stat];
    }
    else
    {
    }

    return res;
}
#undef AS_STRING
