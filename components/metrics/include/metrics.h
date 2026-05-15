/**
 * @file metrics.h
 * @ingroup utilities_module
 * @author Abanoub Salah
 * @brief System-wide metrics/telemetry module
 *
 * @details
 * - Provides lightweight counters for tracking system behavior
 * - Thread-safe and designed for multi-tasking environments
 */

#ifndef METRICS_H
#define METRICS_H

#include "esp_err.h"
#include "stdint.h"

/**
 * @brief Metrics table
 *
 * @note
 * - Entry format as follows
 *     - ENTRY (stat_enum, stat_name)
 * - Where
 *     - stat_enum: Enum used within code
 *     - stat_name: Displayed name of the stat
 */
#define METRICS_TABLE(ENTRY)                                                                                 \
    ENTRY (METRICS_STAT_RS485_DRIVER_OVERFLOW_ERRORS, "RS485_DRIVER_OVERFLOW_ERRORS")                        \
    ENTRY (METRICS_STAT_RS485_DRIVER_PARITY_ERRORS, "RS485_DRIVER_PARITY_ERRORS")                            \
    ENTRY (METRICS_STAT_MODBUS_CRC_ERRORS, "MODBUS_CRC_ERRORS")                                              \
    ENTRY (METRICS_STAT_MODBUS_OVERFLOW_ERRORS, "MODBUS_OVERFLOW_ERRORS")                                    \
    ENTRY (METRICS_STAT_MODBUS_NO_MEM_ERRORS, "MODBUS_NO_MEM_ERRORS")                                        \
    ENTRY (METRICS_STAT_TELEMETRY_PUBLISH_ERRORS, "TELEMETRY_PUBLISH_ERRORS")                                \
    ENTRY (METRICS_STAT_LOGGER_WRITE_ERRORS, "LOGGER_WRITE_ERRORS")

/**
 * @brief Metrics stats Enums
 *
 * @note To add a stat simply add an entry to METRICS_TABLE above
 * and start using it within your code
 */
typedef enum {
/** Extract stat ID from table */
#define AS_ENUM(id, str) id,
    METRICS_TABLE (AS_ENUM)
#undef AS_ENUM
        METRICS_STAT_MAX
} metrics_stat_t;

/**
 * @brief Initialize metrics
 *
 * @details Initialize metrics counters to zero and subscribe
 * to event bus to collect different system stats
 *
 * @return esp_err_t Initialize result
 * @retval ESP_OK Initialize success
 * @retval ESP_ERR_NO_MEM Subscription fails
 */
esp_err_t metrics_init (void);

/**
 * @brief Get a snapshot of current system metrics
 *
 * @details Get a snapshot of current system metrics by copying
 * stats to user provided buffer it's length has to be
 * equal to metric stat maximum length and same counters used
 * type (e.g. uint32_t)
 *
 * @param buf_ptr Pointer to a buffer
 * @param buf_length Length of the buffer
 * @param copied_length Pointer to assign copied length by function
 *
 * @return esp_err_t Get snapshot result
 * @retval ESP_OK Metrics copy success
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 *
 * @note Stats can be accessed using their enums as indexes to
 * access them in copied buffer
 */
esp_err_t metrics_get_snapshot (uint32_t *buf_ptr, size_t buf_length, uint16_t *copied_length);

/**
 * @brief Logs metrics stats through ESP logging facilities
 */
void metrics_log_all (void);

/**
 * @brief Resets metrics counters to zero
 */
void metrics_reset_all (void);

/**
 * @brief Gets metric state name
 *
 * @param stat Metric stat
 *
 * @return char* string name of the specified stat
 */
const char *metrics_stat_to_string (metrics_stat_t stat);

#endif
