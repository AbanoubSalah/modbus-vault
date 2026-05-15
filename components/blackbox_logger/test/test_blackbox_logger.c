#include "blackbox_logger.h"
#include "esp_timer.h"
#include "mock_partition.h"
#include "unity.h"
#include "utils.h"

#include <inttypes.h>

#define TEST_LOGGER_SECTOR_SIZE (4096U)
#define TEST_LOGGER_BATCH_SIZE  (1024U)

static blackbox_logger_t logger;
static uint32_t iteration_case_count = 0;
extern mock_partition_t mock_flash;
static esp_partition_t test_partition = {.size = MOCK_FLASH_SIZE};
static uint8_t sector_buf[TEST_LOGGER_SECTOR_SIZE];
static uint8_t batch_buf[TEST_LOGGER_BATCH_SIZE];
static blackbox_logger_config_t config = {.partition_ptr          = &test_partition,
                                          .sector_buf_ptr         = sector_buf,
                                          .sector_buf_size        = sizeof (sector_buf),
                                          .batch_buf_ptr          = batch_buf,
                                          .batch_buf_size         = sizeof (batch_buf),
                                          .address_align          = 4,
                                          .flush_timer_timeout_us = 5000000,
                                          .write_func             = mock_write,
                                          .read_func              = mock_read,
                                          .erase_func             = mock_erase_range};

/**
 * @brief Reset test configuration parameters
 */
static void test_blackbox_logger_reset_configuration (void)
{
    // Reset configuration
    config.parameters.write_offset  = 0;
    config.parameters.replay_offset = 0;
    config.parameters.last_id       = 0;
}

/**
 * @brief Reset flash and configuration parameters for test
 */
static void test_blackbox_logger_reset_flash (void)
{
    // Erase flash
    (void) memset (&mock_flash, 0xFF, MOCK_FLASH_SIZE);
}

/**
 * @brief Writes entry to Logger for test
 *
 * @param data_ptr Pointer to data
 * @param length Data size
 */
static void test_blackbox_logger_write_entry (uint8_t *data_ptr, uint16_t length)
{
    blackbox_logger_entry_view_t entry = {.data_ptr = data_ptr, .length = length};

    blackbox_logger_err_t err = blackbox_logger_write (&logger, &entry);
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, err);
}

/**
 * @brief Callback for iterator tests
 *
 * @param arg_void_ptr Pointer to callback arguments
 * @param entry_ptr Pointer to Logger entry
 *
 * @return true for acknowledge false otherwise
 */
static bool test_blackbox_logger_cb (void *arg_void_ptr, const blackbox_logger_entry_view_t *entry_ptr)
{
    (void) arg_void_ptr;
    (void) entry_ptr;
    ++iteration_case_count;
    return true;
}

TEST_CASE ("Write and read single entry", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[] = {1, 2, 3, 4, 5};

    test_blackbox_logger_write_entry (data, sizeof (data));
    blackbox_logger_flush (&logger);

    uint8_t out[16]                         = {0};
    blackbox_logger_entry_view_t read_entry = {.data_ptr = out};

    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_read (&logger, 0, &read_entry, sizeof (out)));

    TEST_ASSERT_EQUAL_UINT (sizeof (data), read_entry.length);
    TEST_ASSERT_EQUAL_UINT8_ARRAY (data, out, sizeof (data));

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("CRC should fail with corrupt data", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[] = {0xAA, 0xBB, 0xCC};
    test_blackbox_logger_write_entry (data, sizeof (data));
    blackbox_logger_flush (&logger);

    // Corrupt data in flash
    // This is a hack to corrupt data but not the header
    mock_flash.memory[13] ^= 0xFF;

    uint8_t out[16];
    blackbox_logger_entry_view_t read_entry = {.data_ptr = out};

    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_ERR_INVALID_CRC,
                       blackbox_logger_read (&logger, 0, &read_entry, sizeof (out)));

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("End magic corruption should fail", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[] = {1, 2, 3};
    test_blackbox_logger_write_entry (data, sizeof (data));
    blackbox_logger_flush (&logger);

    // Corrupt end magic
    // This is a hack to corrupt end magic
    size_t offset = 12U + sizeof (data) + sizeof (uint16_t);
    mock_flash.memory[offset] ^= 0xFF;

    uint8_t out[16];
    blackbox_logger_entry_view_t read_entry = {.data_ptr = out};

    TEST_ASSERT_NOT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_read (&logger, 0, &read_entry, sizeof (out)));

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Multiple entries iteration", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t d1[] = {1};
    uint8_t d2[] = {2};
    uint8_t d3[] = {3};

    test_blackbox_logger_write_entry (d1, sizeof (d1));
    test_blackbox_logger_write_entry (d2, sizeof (d2));
    test_blackbox_logger_write_entry (d3, sizeof (d3));
    blackbox_logger_flush (&logger);

    uint8_t buffer[8];
    blackbox_logger_entry_view_t entry = {.data_ptr = buffer};

    iteration_case_count = 0;
    TEST_ASSERT_EQUAL (
        BLACKBOX_LOGGER_ERR_INVALID_ENTRY,
        blackbox_logger_iterate_replay (&logger, test_blackbox_logger_cb, NULL, &entry, sizeof (buffer)));
    TEST_ASSERT_EQUAL (3U, iteration_case_count);

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Wraparound write", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[32];
    (void) memset (data, 0xAB, sizeof (data));

    // Fill partition
    for (uint32_t idx = 0; idx < ((test_partition.size + sizeof (data)) / sizeof (data)); ++idx)
    {
        test_blackbox_logger_write_entry (data, sizeof (data));
    }
    blackbox_logger_flush (&logger);

    uint8_t out[64];
    blackbox_logger_entry_view_t entry = {.data_ptr = out};

    // Should still be readable at start after wrap
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_read (&logger, 0, &entry, sizeof (out)));

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Recovery after reboot", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data1[] = {0x11};
    uint8_t data2[] = {0x22};

    test_blackbox_logger_write_entry (data1, sizeof (data1));
    test_blackbox_logger_write_entry (data2, sizeof (data2));

    // Simulate reboot
    blackbox_logger_deinit (&logger);
    test_blackbox_logger_reset_configuration ();

    blackbox_logger_t new_logger;
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&new_logger, &config));

    uint8_t out[8];
    blackbox_logger_entry_view_t entry = {.data_ptr = out};

    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_read (&new_logger, 0, &entry, sizeof (out)));

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Batch flush triggered", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[128];
    (void) memset (data, 0x55, sizeof (data));

    // Force batch overflow
    for (uint32_t idx = 0; idx <= (TEST_LOGGER_BATCH_SIZE / sizeof (data)); ++idx)
    {
        test_blackbox_logger_write_entry (data, sizeof (data));
    }

    // Since write_offset moves when flush succeed
    TEST_ASSERT_GREATER_THAN (0, logger.write_offset);

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Reject oversized entry", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t big[TEST_LOGGER_BATCH_SIZE + 10];

    blackbox_logger_entry_view_t entry = {.data_ptr = big, .length = sizeof (big)};

    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_ERR_NO_MEM, blackbox_logger_write (&logger, &entry));

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Recovery skips torn write (power loss)", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t good1[] = {0xAA};
    uint8_t good2[] = {0xBB};

    test_blackbox_logger_write_entry (good1, sizeof (good1));

    // Simulate partial write
    mock_inject_write_failure (25);
    test_blackbox_logger_write_entry (good2, sizeof (good2));
    blackbox_logger_flush (&logger);

    // Simulate reboot
    test_blackbox_logger_reset_configuration ();

    blackbox_logger_t new_logger;
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&new_logger, &config));

    uint8_t out[8];
    blackbox_logger_entry_view_t entry = {.data_ptr = out};

    // Only one call should be registered
    iteration_case_count = 0;
    blackbox_logger_iterate_replay (&new_logger, test_blackbox_logger_cb, NULL, &entry, sizeof (out));
    TEST_ASSERT_EQUAL (1U, iteration_case_count);

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Fuzz: random corruption resilience", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[16];
    (void) memset (data, 0xAB, sizeof (data));

    // Fill partition
    for (uint32_t idx = 0; idx < ((test_partition.size + sizeof (data)) / sizeof (data)); ++idx)
    {
        test_blackbox_logger_write_entry (data, sizeof (data));
    }

    // Randomly corrupt flash
    for (uint32_t idx = 0; idx < 50U; ++idx)
    {
        size_t idx                = rand () % mock_flash.size;
        const uint8_t random_byte = (uint8_t) rand () & 0xFFU;
        mock_flash.memory[idx] ^= random_byte;
    }

    blackbox_logger_t new_logger;
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&new_logger, &config));

    uint8_t out[32];
    blackbox_logger_entry_view_t entry = {.data_ptr = out};

    // Should NOT crash or loop infinitely
    for (uint32_t idx = 0; idx < 5U; ++idx)
    {
        blackbox_logger_err_t err =
            blackbox_logger_iterate_replay (&logger, test_blackbox_logger_cb, NULL, &entry, sizeof (out));

        if (err != BLACKBOX_LOGGER_OK)
        {
            break;
        }
    }

    // survival test
    TEST_ASSERT_TRUE (true);

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Benchmark: write throughput", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[64];
    (void) memset (data, 0xCD, sizeof (data));

    uint32_t iterations = 200U;

    int64_t start = esp_timer_get_time ();

    for (uint32_t idx = 0; idx < iterations; ++idx)
    {
        test_blackbox_logger_write_entry (data, sizeof (data));
    }

    int64_t end = esp_timer_get_time ();

    int64_t duration_us = end - start;

    printf ("\nExecution time of logging %" PRIu16 " byte(s) %" PRIu32 " times: %" PRIi64 " us\n",
            sizeof (data), iterations, duration_us);
    TEST_ASSERT_TRUE (duration_us > 0);

    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Long run stability (wrap + reuse)", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[32];
    (void) memset (data, 0xEF, sizeof (data));

    for (uint32_t idx = 0; idx < 1000U; ++idx)
    {
        test_blackbox_logger_write_entry (data, sizeof (data));
    }

    blackbox_logger_deinit (&logger);

    blackbox_logger_t new_logger;
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&new_logger, &config));

    uint8_t out[64];
    blackbox_logger_entry_view_t entry = {.data_ptr = out};

    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_read (&new_logger, 0, &entry, sizeof (out)));

    blackbox_logger_deinit (&new_logger);
}

TEST_CASE ("ID Wrap-around recovery", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();

    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    // Manually force the ID near the wrap point
    logger.last_id = 0xFFFFFFFE;

    uint8_t data[] = {0x11, 0x22};
    test_blackbox_logger_write_entry (data, sizeof (data));
    // Should be 0xFFFFFFFF
    test_blackbox_logger_write_entry (data, sizeof (data));
    // Should wrap to 0x00000000
    blackbox_logger_flush (&logger);

    // Simulate reboot
    blackbox_logger_deinit (&logger);
    blackbox_logger_t new_logger;
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&new_logger, &config));

    // Check if recovery correctly identified the wrapped ID
    TEST_ASSERT_EQUAL_UINT32 (0, new_logger.last_id);
    blackbox_logger_deinit (&new_logger);
}

TEST_CASE ("Perfect sector boundary alignment", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    // We need to fill almost a full sector (4096 bytes)
    // Assume entry overhead is constant (e.g., 12 bytes header + 6 bytes tail)
    // 16 * (238 + 12 + 6) = 4096
    uint8_t dummy_data[238];
    memset (dummy_data, 0xaa, sizeof (dummy_data));

    // Fill sector until we are near the end
    for (int idx = 0; idx < 15; ++idx)
    {
        test_blackbox_logger_write_entry (dummy_data, sizeof (dummy_data));
    }
    blackbox_logger_flush (&logger);

    // The next write should trigger a wrap to the next sector or handle the boundary
    test_blackbox_logger_write_entry (dummy_data, sizeof (dummy_data));
    blackbox_logger_flush (&logger);

    // Verify write_offset is at least into the second sector
    TEST_ASSERT_GREATER_OR_EQUAL (TEST_LOGGER_SECTOR_SIZE, logger.write_offset);
    blackbox_logger_deinit (&logger);
}

TEST_CASE ("Recovery from corrupted parameters hint", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[] = {0xAA, 0xBB};
    for (int idx = 0; idx < 10; ++idx)
    {
        test_blackbox_logger_write_entry (data, sizeof (data));
    }
    blackbox_logger_flush (&logger);
    uint32_t true_offset = logger.write_offset;

    blackbox_logger_deinit (&logger);

    // Manually corrupt the hint to point to middle of nowhere
    config.parameters.write_offset = 0x1234;

    blackbox_logger_t recovery_logger;
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&recovery_logger, &config));

    // Verify recovery found the true offset despite the bad hint
    TEST_ASSERT_EQUAL_UINT32 (true_offset, recovery_logger.write_offset);
    blackbox_logger_deinit (&recovery_logger);
}

TEST_CASE ("Stale data ghosting prevention", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[128];
    memset (data, 0xDD, sizeof (data));

    // Fill partition
    for (uint32_t idx = 0; idx < ((test_partition.size + sizeof (data)) / sizeof (data)); ++idx)
    {
        test_blackbox_logger_write_entry (data, sizeof (data));
    }
    blackbox_logger_flush (&logger);

    // Wrap around and write 5 more entries
    for (uint32_t idx = 0; idx < 5; ++idx)
    {
        test_blackbox_logger_write_entry (data, sizeof (data));
    }
    blackbox_logger_flush (&logger);
    uint32_t last_id_before_reboot = logger.last_id;

    blackbox_logger_deinit (&logger);

    // Recover and ensure it didn't jump into stale IDs from the first lap
    blackbox_logger_t new_logger;
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&new_logger, &config));
    TEST_ASSERT_EQUAL_UINT32 (last_id_before_reboot, new_logger.last_id);

    blackbox_logger_deinit (&new_logger);
}

TEST_CASE ("Flush resilience on hardware failure", "[blackbox]")
{
    test_blackbox_logger_reset_flash ();
    test_blackbox_logger_reset_configuration ();
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_OK, blackbox_logger_init (&logger, &config));

    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    test_blackbox_logger_write_entry (data, sizeof (data));

    // Inject a failure that happens during the flush write
    mock_inject_write_failure (0);

    // Flush should fail, but data should remain in RAM
    TEST_ASSERT_EQUAL (BLACKBOX_LOGGER_ERR_WRITE_FAIL, blackbox_logger_flush (&logger));

    // Clear failure and try again - it should now succeed because data is still in buffer
    blackbox_logger_flush (&logger);

    TEST_ASSERT_GREATER_THAN (0U, logger.write_offset);
    TEST_ASSERT_EQUAL (0U, logger.batch_len);
    blackbox_logger_deinit (&logger);
}
