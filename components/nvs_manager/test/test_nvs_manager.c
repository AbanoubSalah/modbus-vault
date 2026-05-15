#include "esp_err.h"
#include "mock_nvs.h"
#include "nvs_manager.h"
#include "unity.h"

#include <string.h>

nvs_manager_ops_t nvs_ops = {.init   = ram_init,
                             .open   = ram_open,
                             .read   = ram_read,
                             .write  = ram_write,
                             .erase  = ram_erase,
                             .commit = ram_commit,
                             .close  = ram_close,
                             .deinit = ram_deinit};

/**
 * @brief Reset NVS between tests
 */
static void test_nvs_manager_reset_nvs (void)
{
    nvs_manager_init (&nvs_ops);
}

TEST_CASE ("NVS Manager Initialization", "[nvs_config]")
{
    esp_err_t ret = nvs_manager_init (&nvs_ops);

    TEST_ASSERT_EQUAL (ESP_OK, ret);
}

TEST_CASE ("NVS Read/Write WiFi Credentials", "[nvs_config]")
{
    test_nvs_manager_reset_nvs ();

    char test_ssid[]                                   = "Test_AP";
    char test_pass[]                                   = "password123";
    char read_ssid[NVS_MANAGER_MAX_WIFI_SSID_SIZE]     = {0};
    char read_pass[NVS_MANAGER_MAX_WIFI_PASSWORD_SIZE] = {0};

    TEST_ASSERT_EQUAL (ESP_OK,
                       nvs_manager_write_cfg (NVS_MANAGER_KEYS_WIFI_SSID, test_ssid, sizeof (test_ssid)));
    TEST_ASSERT_EQUAL (ESP_OK,
                       nvs_manager_write_cfg (NVS_MANAGER_KEYS_WIFI_PASS, test_pass, sizeof (test_pass)));

    TEST_ASSERT_EQUAL (ESP_OK, nvs_manager_read_cfg (NVS_MANAGER_KEYS_WIFI_SSID, read_ssid));
    TEST_ASSERT_EQUAL (ESP_OK, nvs_manager_read_cfg (NVS_MANAGER_KEYS_WIFI_PASS, read_pass));

    TEST_ASSERT_EQUAL_STRING (test_ssid, read_ssid);
    TEST_ASSERT_EQUAL_STRING (test_pass, read_pass);
}

TEST_CASE ("NVS Read/Write Numeric Values", "[nvs_config]")
{
    test_nvs_manager_reset_nvs ();

    uint32_t write_off = 1024;
    uint32_t read_off  = 0;
    uint32_t write_seq = 55;
    uint32_t read_seq  = 0;

    TEST_ASSERT_EQUAL (
        ESP_OK, nvs_manager_write_cfg (NVS_MANAGER_KEYS_BB_WRITE_OFFSET, &write_off, sizeof (write_off)));
    TEST_ASSERT_EQUAL (ESP_OK, nvs_manager_read_cfg (NVS_MANAGER_KEYS_BB_WRITE_OFFSET, &read_off));
    TEST_ASSERT_EQUAL (write_off, read_off);

    TEST_ASSERT_EQUAL (ESP_OK,
                       nvs_manager_write_cfg (NVS_MANAGER_KEYS_BB_LAST_ID, &write_seq, sizeof (write_seq)));
    TEST_ASSERT_EQUAL (ESP_OK, nvs_manager_read_cfg (NVS_MANAGER_KEYS_BB_LAST_ID, &read_seq));
    TEST_ASSERT_EQUAL (write_seq, read_seq);
}

TEST_CASE ("NVS Out of Bounds Key", "[nvs_config]")
{
    uint32_t val = 100;
    //
    TEST_ASSERT_EQUAL (ESP_ERR_INVALID_ARG, nvs_manager_write_cfg (NVS_MANAGER_KEYS_MAX, &val, sizeof (val)));
    TEST_ASSERT_EQUAL (ESP_ERR_INVALID_ARG, nvs_manager_read_cfg (NVS_MANAGER_KEYS_MAX, &val));
}

TEST_CASE ("NVS Manager CRC Integrity", "[nvs_config]")
{
    // This test simulates a reboot and reload to verify CRC calculation/validation
    test_nvs_manager_reset_nvs ();

    uint32_t offset = 5000;
    esp_err_t ret   = nvs_manager_write_cfg (NVS_MANAGER_KEYS_BB_REPLAY_OFFSET, &offset, sizeof (offset));
    TEST_ASSERT_EQUAL (ESP_OK, ret);

    // Manually trigger the sync logic usually handled by timer
    ret = nvs_manager_flush_cfg ();
    TEST_ASSERT_EQUAL (ESP_OK, ret);

    // Deinit and Re-init to force a reload from flash
    nvs_flash_deinit ();
    ret = nvs_manager_init (&nvs_ops);

    uint32_t loaded_offset = 0;
    nvs_manager_read_cfg (NVS_MANAGER_KEYS_BB_REPLAY_OFFSET, &loaded_offset);

    // If CRC failed, init would have returned an error or reset config
    TEST_ASSERT_EQUAL (ESP_OK, ret);
    TEST_ASSERT_EQUAL (offset, loaded_offset);
}
