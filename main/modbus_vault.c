/**
 * @file modbus_vault.c
 * @ingroup main_module
 * @author Abanoub Salah
 *
 * @brief Main entry to the project
 *
 * @details
 * - Initialize debug pins then
 * - Calls controller
 */
#include "controller.h"
#include "debug_pins.h"
#include "esp_err.h"
#include "esp_log.h"

#include <inttypes.h>

/** Main TAG name */
static const char *TAG = "MAIN";

/**
 * @brief Main entry to the app
 *
 * @details Initialize debug bins then give control to the controller
 */
void app_main (void)
{
    if (init_debug_pins () != ESP_OK)
    {
        ESP_LOGI (TAG, "Debug pins failed to initialize");
    }
    controller_run ();
    // Shouldn't reach here
    ESP_LOGE (TAG, "System exited with controller state: %" PRIi32, controller_get_state ());
}
