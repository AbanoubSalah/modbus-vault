/**
 * @file ble_provisioner.h
 * @ingroup controller_module
 * @author Abanoub Salah
 *
 * @brief BLE provisioner interface
 */

#ifndef BLE_PROVISIONER_H
#define BLE_PROVISIONER_H

#include "esp_err.h"

/**
 * @brief Start BLE provisioner
 *
 * @return esp_err_t provisioning result
 * @retval ESP_OK Provision success
 * @retval ESP_FAIL Provision fail
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_INVALID_STATE Configuration in invalid state
 * @retval ESP_ERR_NO_MEM No available memory for resource allocation
 */
esp_err_t ble_provisioner_start (void);

#endif
