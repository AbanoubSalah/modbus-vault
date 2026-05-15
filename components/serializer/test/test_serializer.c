#include "modbus.pb-c.h"
#include "serializer.h"
#include "slab_pool.h"
#include "unity.h"

#include <string.h>

TEST_CASE ("Serializer Encode Modbus Success", "[serializer]")
{
    // Setup the input frame
    slab_pool_t raw_data = {.data   = {0x01, 0x03, 0x00, 0x64, 0x00, 0x01}, // Example Modbus RTU
                            .length = 6};
    slab_pool_t serialized_data;

    // Perform packing
    esp_err_t success = serializer_pack (&raw_data, 1711972800000, &serialized_data);
    TEST_ASSERT_EQUAL (ESP_OK, success);
    TEST_ASSERT_GREATER_THAN (0, serialized_data.length);

    // Unpack and Verify using the generated Protobuf-C API
    ModbusFrame *unpacked = modbus_frame__unpack (NULL, serialized_data.length, serialized_data.data);
    TEST_ASSERT_NOT_NULL (unpacked);

    // Verify fields match the original
    TEST_ASSERT_EQUAL (raw_data.length, unpacked->data.len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (raw_data.data, unpacked->data.data, raw_data.length);

    modbus_frame__free_unpacked (unpacked, NULL);
}
