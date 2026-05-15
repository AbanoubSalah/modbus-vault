/**
 * @file debug_pins.c
 * @ingroup debug_module
 * @brief Implementation of the debug pins
 *
 * @details Initializes debug pins as output for toggling
 */

#include "debug_pins.h"

#include "esp_log.h"

#ifdef CONFIG_DEBUG_PINS_ENABLE
#define AS_NUM(pin_name, pin_num) pin_num,
static gpio_num_t pins_list[] = {PINS_TABLE (AS_NUM)};
int8_t pins_list_count        = (sizeof (pins_list) / sizeof (*pins_list));
#undef AS_NUM
#endif

esp_err_t init_debug_pins (void)
{
#ifdef CONFIG_DEBUG_PINS_ENABLE
    // Configuration structure
    gpio_config_t io_conf = {
        // Set as output mode
        .mode = GPIO_MODE_OUTPUT,
        // Disable internal pull-ups/downs for high-speed toggling
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        // Disable interrupts on these pins
        .intr_type = GPIO_INTR_DISABLE,
    };

    // Bit mask for all pins to configure as outputs
    uint64_t pins_bit_mask = 0ULL;
    for (int8_t idx = 0; idx < pins_list_count; ++idx)
    {
        pins_bit_mask |= (1ULL << pins_list[idx]);
    }
    io_conf.pin_bit_mask = pins_bit_mask;

    // Apply configuration
    esp_err_t err = gpio_config (&io_conf);

    if (err != ESP_OK)
    {
        ESP_LOGE ("DEBUG", "Failed to configure GPIOs: %s", esp_err_to_name (err));
    }
    else
    {
        // Ensure pins start at Logic LOW
        for (int8_t idx = 0; ((err == ESP_OK) && (idx < pins_list_count)); ++idx)
        {
            err = gpio_set_level (pins_list[idx], 0);
        }
    }

    return err;
#else
    return ESP_OK;
#endif
}
