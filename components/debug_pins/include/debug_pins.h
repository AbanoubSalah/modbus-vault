/**
 * @file debug_pins.h
 * @ingroup debug_module
 * @author Abanoub Salah
 * @brief Debug using GPIO toggling
 *
 * @details This module handles debugging code using pins
 * toggling for different modules.
 */
#ifndef DEBUG_PINS_H
#define DEBUG_PINS_H

#include "driver/gpio.h"
#include "sdkconfig.h"

// Define the GPIO numbers in one place
/**
 * @brief Debug pins table
 *
 * @note
 * - Entry format as follows
 *     - ENTRY (pin_name, pin_num)
 * - Where
 *     - pin_name: Enum used within code
 *     - pin_num: Pin number
 */
#define PINS_TABLE(ENTRY)                                                                                    \
    ENTRY (DEBUG_PINS_FRAME_CAPTURE, (GPIO_NUM_6))                                                           \
    ENTRY (DEBUG_PINS_FRAME_SERIALIZED, (GPIO_NUM_7))                                                        \
    ENTRY (DEBUG_PINS_FRAME_PUBLISHED, (GPIO_NUM_8))                                                         \
    ENTRY (DEBUG_PINS_FRAME_LOGGED, (GPIO_NUM_9))

/**
 * @brief Debug pins Enums
 *
 * @note To add a pin simply add an entry to PINS_TABLE above
 * and start using it within your code
 */
typedef enum {
/** We will extract both name and number from table */
#define AS_ENUM(pin_name, pin_num) pin_name = pin_num,
    PINS_TABLE (AS_ENUM)
#undef AS_ENUM
} debug_pins_t;

#ifdef CONFIG_DEBUG_PINS_ENABLE
//! Set debug pin high
#define DEBUG_GPIO_SET(pin) gpio_set_level (pin, 1)
//! Set debug pin low
#define DEBUG_GPIO_CLR(pin) gpio_set_level (pin, 0)
#else
/** Does nothing */
#define DEBUG_GPIO_SET(pin)
/** Does nothing */
#define DEBUG_GPIO_CLR(pin)
#endif

/**
 * @brief Initiate debug pins if any.
 *
 * @return esp_err_t Initialize result
 * @retval ESP_OK Initialize success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 */
esp_err_t init_debug_pins (void);

#endif
