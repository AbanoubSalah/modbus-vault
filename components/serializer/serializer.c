/**
 * @file serializer.c
 * @ingroup utilities_module
 * @brief Implementation of the serializer
 *
 * @details Serialize Modbus frame into predefined structure ready to send
 */

#include "serializer.h"

#include "modbus.pb-c.h"

esp_err_t serializer_pack (const slab_pool_t *src_ptr, int64_t timestamp_us, slab_pool_t *dst_ptr)
{
    esp_err_t err = ESP_OK;
    if ((src_ptr == NULL) || (dst_ptr == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    else
    {
        ModbusFrame msg = MODBUS_FRAME__INIT;

        msg.timestamp_us = timestamp_us;

        msg.data.data = (uint8_t *) src_ptr->data;
        msg.data.len  = src_ptr->length;

        // Extract metadata from the raw Modbus PDU
        if (src_ptr->length >= 2)
        {
            msg.slave_address = src_ptr->data[0];
            msg.function_code = src_ptr->data[1];
        }

        size_t needed = modbus_frame__get_packed_size (&msg);
        if (needed <= SLAB_POOL_MAX_DATA_SIZE)
        {
            modbus_frame__pack (&msg, dst_ptr->data);
            dst_ptr->length = needed;
        }
        else
        {
            err = ESP_ERR_NO_MEM;
        }
    }

    return err;
}
