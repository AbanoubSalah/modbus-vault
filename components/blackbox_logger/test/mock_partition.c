#include "mock_partition.h"

mock_partition_t mock_flash = {.size = MOCK_FLASH_SIZE, .fail_next_write = false};

void mock_flash_reset (void)
{
    (void) memset (mock_flash.memory, 0xFF, sizeof (mock_flash.memory));
    mock_flash.size             = MOCK_FLASH_SIZE;
    mock_flash.fail_next_write  = false;
    mock_flash.fail_after_bytes = 0;
}

esp_err_t mock_read (const esp_partition_t *partition_ptr, size_t offset, void *data_void_ptr, size_t size)
{
    (void) partition_ptr;
    esp_err_t err = ESP_OK;
    if ((offset + size) > mock_flash.size)
    {
        err = ESP_ERR_INVALID_SIZE;
    }
    else
    {
        (void) memcpy (data_void_ptr, mock_flash.memory + offset, size);
    }

    return err;
}

esp_err_t
mock_write (const esp_partition_t *partition_ptr, size_t offset, const void *data_void_ptr, size_t size)
{
    (void) partition_ptr;
    esp_err_t err = ESP_OK;

    if (data_void_ptr == NULL)
    {
        err = ESP_ERR_INVALID_ARG;
    }

    if ((offset + size) > mock_flash.size)
    {
        err = ESP_ERR_INVALID_SIZE;
    }
    else
    {
        const uint8_t *const src = (const uint8_t *) data_void_ptr;

        for (size_t idx = 0U; ((idx < size) && (err == ESP_OK)); ++idx)
        {
            const size_t addr = offset + idx;

            if (mock_flash.fail_next_write && (idx >= mock_flash.fail_after_bytes))
            {
                mock_flash.fail_next_write = false;
                err                        = ESP_FAIL;
            }
            else
            {
                const uint8_t old_val = mock_flash.memory[addr];
                const uint8_t new_val = src[idx];

                // Physical NOR Flash Rule: bits can only transition from 1 to 0
                if ((new_val & (uint8_t) (~old_val)) != 0U)
                {
                    // Attempting to turn a 0 into a 1 without an erase
                    err = ESP_ERR_INVALID_STATE;
                }
                else
                {
                    // Perform the bitwise AND (Physical behavior of Flash transistors)
                    mock_flash.memory[addr] = (old_val & new_val);
                }
            }
        }
    }

    return err;
}

esp_err_t mock_erase_range (const esp_partition_t *partition_ptr, size_t offset, size_t size)
{
    (void) partition_ptr;
    esp_err_t err = ESP_OK;
    if (((offset % MOCK_SECTOR_SIZE) != 0) || ((size % MOCK_SECTOR_SIZE) != 0))
    {
        err = ESP_ERR_INVALID_ARG;
    }
    else if ((offset + size) > mock_flash.size)
    {
        err = ESP_ERR_INVALID_SIZE;
    }
    else
    {
        (void) memset (mock_flash.memory + offset, 0xFF, size);
    }

    return err;
}

void mock_inject_write_failure (size_t after_bytes)
{
    mock_flash.fail_next_write  = true;
    mock_flash.fail_after_bytes = after_bytes;
}
