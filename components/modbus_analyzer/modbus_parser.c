/**
 * @file modbus_parser.c
 * @ingroup modbus_analyzer_module
 * @brief Implementation of the modbus parser
 */

#include "modbus_parser.h"

#include "utils.h"

esp_err_t modbus_parser_check_crc (const uint8_t *data_ptr, size_t length)
{
    esp_err_t err = ESP_OK;

    if (data_ptr == NULL)
    {
        err = ESP_ERR_INVALID_ARG;
    }
    else if (length < MODBUS_PARSER_MINIMUM_FRAME_LENGTH)
    {
        err = ESP_ERR_INVALID_SIZE;
    }
    else
    {
        // Taking into account Modbus endianess
        uint16_t calc = calculate_modbus_crc16 (data_ptr, length - 2U);
        uint16_t recv = (data_ptr[length - 1U] << 8U) | data_ptr[length - 2U];
        err           = ((calc == recv) ? ESP_OK : ESP_ERR_INVALID_CRC);
    }

    return err;
}
