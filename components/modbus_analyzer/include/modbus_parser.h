/**
 * @file modbus_parser.h
 * @ingroup modbus_analyzer_module
 * @author Abanoub Salah
 * @brief Parser for Modbus
 *
 * @details Provides CRC16 check abstraction for Modbus
 */

#ifndef MODBUS_PARSER_H
#define MODBUS_PARSER_H

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

/** Minimum Modbus frame: |Address|Function|CRC16_Lo|CRC16_Hi| */
#define MODBUS_PARSER_MINIMUM_FRAME_LENGTH (4)

/**
 * @brief Checks CRC16-Modbus of data validity
 *
 * @param data_ptr Pointer to data
 * @param length Data length
 *
 * @return esp_err_t CRC check result
 * @retval ESP_OK CRC OK
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_INVALID_SIZE Data size invalid
 * @retval ESP_ERR_INVALID_CRC CRC invalid
 */
esp_err_t modbus_parser_check_crc (const uint8_t *data_ptr, size_t length);

#endif
