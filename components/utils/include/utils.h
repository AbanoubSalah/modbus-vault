/**
 * @file utils.h
 * @ingroup utilities_module
 * @author Abanoub Salah
 *
 * @brief Utility functions
 *
 * @details Contains different project-wide utility functions
 */

#include <stdint.h>

#ifndef UTILS_H
#define UTILS_H

/**
 * @brief Calculate Modbus CRC16
 *
 * @param data_ptr Pointer to data
 * @param length Data length
 *
 * @return uint16_t CRC16 for provided data
 *
 * @note
 * - Can be configured using kconfig to choose between
 *     - Look Up Table (LUT)
 *     - Arithmetic calculation
 */
uint16_t calculate_modbus_crc16 (const uint8_t *data_ptr, uint32_t length);

#endif
