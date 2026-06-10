/**
 * @file telemetry_service.c
 * @ingroup telemetry_module
 * @author Abanoub Salah
 * @brief Implementation of the telemetry service
 *
 * @details
 * - Initializes MQTT bridge instance
 * - Pre-generated publish topics
 * - Pre-set topics QOS
 * - Enqueue live records
 * - Publish live/replay records
 * - Holds MQTT connectivity status
 * - Task process live records in queue and process replay if available
 */

#include "telemetry_service.h"

#include "debug_pins.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "event_bus.h"
#include "freertos/semphr.h"
#include "metrics.h"
#include "runtime_tasks.h"

#define TELEMETRY_SERVICE_LIVE_QUEUE_LENGTH (16U) /**< Live queue length */
#define TELEMETRY_SERVICE_LIVE_QUOTA        (2U)  /**< Live quota */
#define TELEMETRY_SERVICE_REPLAY_QUOTA      (1U)  /**< Replay quota */
#define TELEMETRY_SERVICE_TASK_DELAY_TIME_MS                                                                 \
    (30U) /**< Replay task delay time after a burst in micro seconds */
#define TELEMETRY_SERVICE_PUBLISH_METRICS_PERIOD_US                                                          \
    (300000000UL)                                    /**< Publish system's metrics period in micro seconds */
#define TELEMETRY_SERVICE_FULL_TOPIC_MAX_SIZE (128U) /**< MQTT topics maximum size */

/** Telemetry Service TAG name */
static const char *TAG = "TELEMETRY_SERVICE";

/** Topics preset suffixes */
static const char *topic_suffixes[] = {"/modbus/live", "/modbus/replay", "/modbus/status"};
/** Full topics */
static char topics[TELEMETRY_SERVICE_TOPICS_MAX][TELEMETRY_SERVICE_FULL_TOPIC_MAX_SIZE];
/** Topic QoS */
static uint8_t topics_qos[TELEMETRY_SERVICE_TOPICS_MAX];

/** Telemetry Service task handle */
static TaskHandle_t telemetry_service_task_handle = NULL;
/** Telemetry Service live queue handle */
static QueueHandle_t live_queue = NULL;

/** Telemetry Service state flag lock */
static SemaphoreHandle_t telemetry_service_state_lock = NULL;
/** Telemetry Service state flag */
static bool telemetry_service_is_online_flag = false;

/** MQTT Bridge instance */
static mqtt_bridge_t mqtt_bridge;

/** Timer handle for system's metrics publishing */
esp_timer_handle_t pub_metrics_timer_handle = NULL;

/** Callback for publishing replay */
bool (*publish_replay_cb_func) (void) = NULL;

/**
 * @brief Generate publishing topics
 *
 * @details Generate publishing topics according to a preset template
 * defined by topics base and topic_suffixes
 *
 * @param device_id_ptr Pointer to device id
 *
 * @return esp_err_t Generate result
 * @retval ESP_OK Generate success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_INVALID_SIZE Topic size exceeded maximum
 */
static esp_err_t generate_topics_internal (const char *device_id_ptr)
{
    esp_err_t err = ESP_OK;

    if (device_id_ptr == NULL)
    {
        err = ESP_ERR_INVALID_ARG;
    }

    for (telemetry_service_topics_t topic = 0; ((topic < TELEMETRY_SERVICE_TOPICS_MAX) && (err == ESP_OK));
         ++topic)
    {
        int32_t len = snprintf (topics[topic], TELEMETRY_SERVICE_FULL_TOPIC_MAX_SIZE, "devices/%s%s",
                                device_id_ptr, topic_suffixes[topic]);
        err = (((len <= 0) || (len >= (int32_t) TELEMETRY_SERVICE_FULL_TOPIC_MAX_SIZE)) ? ESP_ERR_INVALID_SIZE
                                                                                        : ESP_OK);
    }

    return err;
}

/**
 * @brief Process live records quota
 */
static void process_live_quota_internal (void)
{
    telemetry_pipeline_record_t payload;
    bool keep_processing = true;

    for (uint32_t idx = 0; ((keep_processing == true) && (idx < TELEMETRY_SERVICE_LIVE_QUOTA)); ++idx)
    {
        keep_processing =
            ((telemetry_service_is_online () == true) && (xQueueReceive (live_queue, &payload, 0) == pdPASS));
        if (keep_processing == true)
        {
            if (telemetry_service_publish_live (&payload) == true)
            {
            }
            slab_pool_free (payload.slab_ptr);
        }
    }
}

/**
 * @brief Process replay records quota
 *
 * @return true on last replay process success false otherwise
 */
static bool process_replay_quota_internal (void)
{
    bool keep_processing = (publish_replay_cb_func != NULL);

    for (uint32_t idx = 0; ((keep_processing == true) && (idx < TELEMETRY_SERVICE_REPLAY_QUOTA)); ++idx)
    {
        keep_processing = ((telemetry_service_is_online () == true) && (publish_replay_cb_func () == true));
        vTaskDelay (0);
    }

    return keep_processing;
}

/**
 * @brief Handle sending system's metrics
 *
 * @details Get system metrics snapshot, stamp it and publish it
 *
 * @param arg_void_ptr Void pointer to argument
 */
static void handle_system_metrics_internal (void *arg_void_ptr)
{
    (void) arg_void_ptr;
    slab_pool_t system_status_buffer;
    telemetry_pipeline_record_t record = {.slab_ptr     = &system_status_buffer,
                                          .timestamp_us = esp_timer_get_time ()};

    if ((metrics_get_snapshot ((uint32_t *) system_status_buffer.data, SLAB_POOL_MAX_DATA_SIZE,
                               &system_status_buffer.length) == ESP_OK) &&
        (telemetry_service_is_online () == true))
    {
        (void) telemetry_service_publish_status (&record);
    }
}

/**
 * @brief Publish to topic
 *
 * @param payload_ptr Pointer to record
 * @param topic Topic
 * @return true if published false otherwise
 */
static bool publish_to_topic_helper (const telemetry_pipeline_record_t *payload_ptr,
                                     telemetry_service_topics_t topic)
{
    bool is_published           = false;
    const slab_pool_t *slab_ptr = payload_ptr->slab_ptr;
    if (mqtt_bridge_publish (&mqtt_bridge, topics[topic], (char *) (slab_ptr->data), slab_ptr->length,
                             topics_qos[topic]) == ESP_OK)
    {
        is_published = true;
    }
    else
    {
        event_bus_t event = {.type         = EVENT_BUS_EVENT_ERROR,
                             .payload.data = METRICS_STAT_TELEMETRY_PUBLISH_ERRORS,
                             .size         = sizeof (metrics_stat_t),
                             .timestamp_us = payload_ptr->timestamp_us};
        event_bus_publish (&event);
    }

    return is_published;
}

/**
 * @brief Process live queue and replay if available
 *
 * @param parameters_void_ptr Void pointer to task parameters
 */
static void telemetry_service_task (void *parameters_void_ptr)
{
    (void) parameters_void_ptr;
    uint32_t ulNotifiedValue = 0;

    // Evaluate initial conditions; if a replay callback exists,
    // force the task to check for replays on boot.
    bool work_pending = true;

    ESP_LOGI (TAG, "Task \"%s\" started", pcTaskGetName (NULL));
    while (true)
    {
        TickType_t xTicksToWait = ((work_pending == true) ? 0 : portMAX_DELAY);
        xTaskNotifyWait (0x00, ULONG_MAX, &ulNotifiedValue, xTicksToWait);

        if ((ulNotifiedValue & TELEMETRY_SERVICE_TASK_NOTIFY_STOP_BIT) != 0)
        {
            ESP_LOGI (TAG, "Task \"%s\" stopped", pcTaskGetName (NULL));
            vTaskDelete (NULL);
        }

        // Process live queue if there are messages
        if (uxQueueMessagesWaiting (live_queue) > 0)
        {
            DEBUG_GPIO_SET (DEBUG_PINS_FRAME_PUBLISHED);
            process_live_quota_internal ();
            DEBUG_GPIO_CLR (DEBUG_PINS_FRAME_PUBLISHED);
        }

        // Process replay queue if online and available
        if (telemetry_service_is_online ())
        {
            bool more_replays = process_replay_quota_internal ();
            if (more_replays == true)
            {
                xTaskNotify (telemetry_service_task_handle, TELEMETRY_SERVICE_TASK_NOTIFY_REPLAY_BIT,
                             eSetBits);
                vTaskDelay (pdMS_TO_TICKS (TELEMETRY_SERVICE_TASK_DELAY_TIME_MS));
            }
        }

        // Re-evaluate pending work for the next loop
        work_pending = (uxQueueMessagesWaiting (live_queue) > 0);
    }
}

/**
 * @brief Stop telemetry service task
 */
static void telemetry_service_stop_task (void)
{
    if (telemetry_service_task_handle != NULL)
    {
        (void) xTaskNotify (telemetry_service_task_handle, TELEMETRY_SERVICE_TASK_NOTIFY_STOP_BIT, eSetBits);
    }
}

/** Telemetry Service task configuration structure */
runtime_task_config_t telemetry_service_task_config = {.name        = TELEMETRY_SERVICE_TASK_NAME,
                                                       .entry       = telemetry_service_task,
                                                       .arg         = NULL,
                                                       .stack_depth = TELEMETRY_SERVICE_TASK_STACK_DEPTH,
                                                       .priority    = TELEMETRY_SERVICE_TASK_PRIORITY,
                                                       .core_id     = TELEMETRY_SERVICE_TASK_CPU_AFFINITY,
                                                       .handle      = &telemetry_service_task_handle,
                                                       .stop_func   = telemetry_service_stop_task};

bool telemetry_service_init (mqtt_bridge_config_t *mqtt_bridge_config_ptr, bool (*replay_check_cb) (void))
{
    bool is_init                     = true;
    telemetry_service_is_online_flag = false;

    if ((mqtt_bridge_config_ptr == NULL) || (replay_check_cb == NULL) ||
        (mqtt_bridge_config_ptr->device_id == NULL))
    {
        is_init = false;
    }

    if (is_init == true)
    {
        telemetry_service_state_lock = xSemaphoreCreateMutex ();
        is_init                      = (telemetry_service_state_lock != NULL);
    }

    if (is_init == true)
    {
        publish_replay_cb_func            = replay_check_cb;
        mqtt_bridge_config_ptr->notify_cb = telemetry_service_set_online;
        is_init = (mqtt_bridge_init (&mqtt_bridge, mqtt_bridge_config_ptr) == ESP_OK);
    }

    if (is_init == true)
    {
        live_queue = xQueueCreate (TELEMETRY_SERVICE_LIVE_QUEUE_LENGTH, sizeof (telemetry_pipeline_record_t));
        is_init    = (live_queue != NULL);
    }

    if (is_init == true)
    {
        is_init = (generate_topics_internal (mqtt_bridge_config_ptr->device_id) == ESP_OK);
        topics_qos[TELEMETRY_SERVICE_TOPICS_LIVE]   = 0;
        topics_qos[TELEMETRY_SERVICE_TOPICS_REPLAY] = 0;
        topics_qos[TELEMETRY_SERVICE_TOPICS_STATUS] = 1;
    }

    if (is_init == true)
    {
        // Timer for publishing systems metrics
        const esp_timer_create_args_t timer_args = {.callback = handle_system_metrics_internal,
                                                    .name     = "pub_metrics_timer"};
        is_init = (esp_timer_create (&timer_args, &pub_metrics_timer_handle) == ESP_OK);
        if (is_init == true)
        {
            // Start periodic timer
            is_init = (esp_timer_start_periodic (pub_metrics_timer_handle,
                                                 TELEMETRY_SERVICE_PUBLISH_METRICS_PERIOD_US) == ESP_OK);
        }
    }

    return is_init;
}

bool telemetry_service_enqueue_live (const telemetry_pipeline_record_t *payload_ptr)
{
    bool is_enqueued = (xQueueSend (live_queue, payload_ptr, 0) == pdTRUE);

    if (is_enqueued == true)
    {
        xTaskNotify (telemetry_service_task_handle, TELEMETRY_SERVICE_TASK_NOTIFY_LIVE_BIT, eSetBits);
    }

    return is_enqueued;
}

bool telemetry_service_publish_live (const telemetry_pipeline_record_t *payload_ptr)
{
    return publish_to_topic_helper (payload_ptr, TELEMETRY_SERVICE_TOPICS_LIVE);
}

bool telemetry_service_publish_replay (const telemetry_pipeline_record_t *payload_ptr)
{

    return publish_to_topic_helper (payload_ptr, TELEMETRY_SERVICE_TOPICS_REPLAY);
}

bool telemetry_service_publish_status (const telemetry_pipeline_record_t *payload_ptr)
{

    return publish_to_topic_helper (payload_ptr, TELEMETRY_SERVICE_TOPICS_STATUS);
}

void telemetry_service_notify_replay_available (void)
{
    xTaskNotify (telemetry_service_task_handle, TELEMETRY_SERVICE_TASK_NOTIFY_REPLAY_BIT, eSetBits);
}

void telemetry_service_set_online (bool online)
{
    if ((telemetry_service_state_lock != NULL) &&
        (xSemaphoreTake (telemetry_service_state_lock, 0) == pdPASS))
    {
        telemetry_service_is_online_flag = online;
        (void) xSemaphoreGive (telemetry_service_state_lock);
    }

    if (online == true)
    {
        xTaskNotify (telemetry_service_task_handle, TELEMETRY_SERVICE_TASK_NOTIFY_ONLINE_BIT, eSetBits);
    }
}

bool telemetry_service_is_online (void)
{
    bool is_online = false;
    if ((telemetry_service_state_lock != NULL) &&
        (xSemaphoreTake (telemetry_service_state_lock, 0) == pdPASS))
    {
        is_online = telemetry_service_is_online_flag;
        (void) xSemaphoreGive (telemetry_service_state_lock);
    }
    return is_online;
}

void telemetry_service_deinit (void)
{
    mqtt_bridge_deinit (&mqtt_bridge);

    if (live_queue != NULL)
    {
        vQueueDelete (live_queue);
        live_queue = NULL;
    }

    if (telemetry_service_state_lock != NULL)
    {
        vSemaphoreDelete (telemetry_service_state_lock);
        telemetry_service_state_lock = NULL;
    }

    if (pub_metrics_timer_handle != NULL)
    {
        (void) esp_timer_stop (pub_metrics_timer_handle);
        (void) esp_timer_delete (pub_metrics_timer_handle);
        pub_metrics_timer_handle = NULL;
    }
}
