/**
 * @file system_init.c
 * @ingroup controller_module
 * @author Abanoub Salah
 *
 * @brief Implementation of the system initialization
 *
 * @details
 * - Initialize components in a preset steps returning on success or first error
 * - Deinitialize components in reverse order of initialization
 */

#include "system_init.h"

#include "event_bus.h"
#include "logger_service.h"
#include "metrics.h"
#include "nvs_manager.h"
#include "router.h"
#include "slab_pool.h"
#include "system_config.h"
#include "telemetry_pipeline.h"
#include "telemetry_service.h"
#include "wifi_manager.h"

esp_err_t system_init (void)
{
    esp_err_t err                                        = ESP_OK;
    modbus_analyzer_config_t *analyzer_config_ptr        = system_config_get_modbus_analyzer_config ();
    mqtt_bridge_config_t *mqtt_config_ptr                = system_config_get_mqtt_config ();
    blackbox_logger_config_t *blackbox_logger_config_ptr = system_config_get_blackbox_logger_config ();
    nvs_manager_ops_t *nvs_ops_ptr                       = system_config_get_nvs_manager_ops ();
    wifi_config_t *wifi_config_ptr                       = system_config_get_wifi_config ();
    init_step_t current_init_step                        = INIT_STEP_DEFAULT_CONFIG;

    while (current_init_step != INIT_STEP_COMPLETE)
    {
        switch (current_init_step)
        {
        case INIT_STEP_DEFAULT_CONFIG:
            err               = system_config_setup_defaults ();
            current_init_step = INIT_STEP_NVS;
            break;
        case INIT_STEP_NVS:
            err               = nvs_manager_init (nvs_ops_ptr);
            current_init_step = INIT_STEP_CONFIG;
            break;
        case INIT_STEP_CONFIG:
            err               = system_config_setup ();
            current_init_step = INIT_STEP_WIFI;
            break;
        case INIT_STEP_WIFI:
            err               = wifi_manager_init (wifi_config_ptr);
            current_init_step = INIT_STEP_SLAB;
            break;
        case INIT_STEP_SLAB:
            err               = slab_pool_init ();
            current_init_step = INIT_STEP_EVENT_BUS;
            break;
        case INIT_STEP_EVENT_BUS:
            err               = event_bus_init (EVENT_BUS_QUEUE_SIZE);
            current_init_step = INIT_STEP_METRICS;
            break;
        case INIT_STEP_METRICS:
            err               = metrics_init ();
            current_init_step = INIT_STEP_TELE_PLIN;
            break;
        case INIT_STEP_TELE_PLIN:
            err = ((telemetry_pipeline_init (analyzer_config_ptr, router_dispatch) == true) ? ESP_OK
                                                                                            : ESP_FAIL);
            current_init_step = INIT_STEP_TELE_SERV;
            break;
        case INIT_STEP_TELE_SERV:
            err = ((telemetry_service_init (mqtt_config_ptr, router_publish_replay) == true) ? ESP_OK
                                                                                             : ESP_FAIL);
            current_init_step = INIT_STEP_LOG_SERV;
            break;
        case INIT_STEP_LOG_SERV:
            err = ((logger_service_init (blackbox_logger_config_ptr, router_on_replay_available_callback) ==
                    true)
                       ? ESP_OK
                       : ESP_FAIL);
            current_init_step = INIT_STEP_COMPLETE;
            break;
        default:
            break;
        }

        if (err != ESP_OK)
        {
            system_deinit ();
            break;
        }
    }

    return err;
}

void system_deinit (void)
{
    telemetry_service_deinit ();
    logger_service_deinit ();
    telemetry_pipeline_deinit ();
    wifi_manager_deinit ();
    event_bus_deinit ();
    slab_pool_deinit ();
    system_config_reset ();
    nvs_manager_deinit ();
}
