/**
 * @file ble_provisioner.c
 * @ingroup controller_module
 * @author Abanoub Salah
 *
 * @brief Implementation of the BLE provisioner
 *
 * @details
 * - Bootstraps a WiFi station with event handler for receiving creds
 * - Only handles provision events
 * - Configure provisioner for BLE and sec1
 * - Starts provisioner
 * - Restarts after successful provisioning
 */

#include "ble_provisioner.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_manager.h"
#include "sdkconfig.h"

#include <network_provisioning/manager.h>
#include <network_provisioning/scheme_ble.h>
#include <string.h>

/** BLE Provisioner TAG name */
static const char *TAG = "BLE_PROV";

/**
 * @brief Handle provisioning events
 *
 * @param arg_ptr Pointer to args
 * @param event_base Event base
 * @param event_id Event ID
 * @param event_data_ptr Pointer to event data
 */
static void provisioning_event_handler (void *arg_ptr,
                                        esp_event_base_t event_base,
                                        int32_t event_id,
                                        void *event_data_ptr)
{
    (void) arg_ptr;
    if (event_base == NETWORK_PROV_EVENT)
    {
        switch (event_id)
        {
        case NETWORK_PROV_WIFI_CRED_RECV: {
            wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *) event_data_ptr;

            ESP_LOGI (TAG, "Credentials received via BLE. Saving to NVS...");

            // Write creds to NVS
            esp_err_t err = nvs_manager_write_cfg (NVS_MANAGER_KEYS_WIFI_SSID, wifi_sta_cfg->ssid,
                                                   strlen ((char *) wifi_sta_cfg->ssid) + 1);
            if (err != ESP_OK)
            {
                ESP_LOGW (TAG, "NVS SSID write failed %s", esp_err_to_name (err));
            }

            err = nvs_manager_write_cfg (NVS_MANAGER_KEYS_WIFI_PASS, wifi_sta_cfg->password,
                                         strlen ((char *) wifi_sta_cfg->password) + 1);
            if (err != ESP_OK)
            {
                ESP_LOGW (TAG, "NVS password write failed %s", esp_err_to_name (err));
            }

            // Flush before reset
            err = nvs_manager_flush_cfg ();
            if (err != ESP_OK)
            {
                ESP_LOGW (TAG, "NVS flushing configuration failed %s", esp_err_to_name (err));
            }
            break;
        }
        case NETWORK_PROV_END:
            network_prov_mgr_deinit ();
            ESP_LOGI (TAG, "Provisioning successful. Restarting system...");

            // Hard restart to start clean
            esp_restart ();
            break;
        }
    }
}

esp_err_t ble_provisioner_start (void)
{
    // Network stack setup
    esp_err_t err = esp_netif_init ();
    if (err == ESP_OK)
    {
        err = esp_event_loop_create_default ();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT ();
    if (err == ESP_OK)
    {
        err = esp_wifi_init (&cfg);
    }

    if (err == ESP_OK)
    {
        if (esp_netif_create_default_wifi_sta () == NULL)
        {
            err = ESP_FAIL;
        }
    }

    if (err == ESP_OK)
    {
        err = esp_wifi_set_mode (WIFI_MODE_STA);
    }

    if (err == ESP_OK)
    {
        // Register provisioning events
        err = esp_event_handler_register (NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &provisioning_event_handler,
                                          NULL);
    }

    if (err == ESP_OK)
    {
        // Setup provisioning manager
        network_prov_mgr_config_t config = {.scheme = network_prov_scheme_ble,
                                            .scheme_event_handler =
                                                NETWORK_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};
        err                              = network_prov_mgr_init (config);
    }

    if (err == ESP_OK)
    {
        // BLE broadcast name
        char device_name[] = CONFIG_SYSTEM_CONFIG_BLE_DEVICE_NAME;

        network_prov_security_t security_ver;
        const void *prov_sec_params = NULL;

        security_ver    = NETWORK_PROV_SECURITY_1;
        prov_sec_params = (const void *) CONFIG_SYSTEM_CONFIG_PROV_POP;

        // Start advertising over BLE
        ESP_LOGI (TAG, "Starting BLE discovery node: %s", device_name);
        err = network_prov_mgr_start_provisioning (security_ver, prov_sec_params, device_name, NULL);
    }

    return err;
}
