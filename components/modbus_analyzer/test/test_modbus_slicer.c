#include "modbus_slicer.h"
#include "slab_pool.h"
#include "unity.h"

#include <string.h>

static int64_t fake_time = 0;
static bool frame_called = false;
static slab_pool_t last_data;
static bool is_setup_done = false;

/**
 * @brief Mock timer
 *
 * @details Get current time
 *
 * @return int64_t Current time
 */
static int64_t test_time_us (void)
{
    return fake_time;
}

/**
 * @brief Callback triggered when a frame is ready
 *
 * @details Copy frame to a static buffer
 *
 * @param frame_ptr Pointer to slicer frame
 * @param arg_void_ptr Void pointer to argument
 */
static void test_frame_cb (const modbus_slicer_frame_t *frame_ptr, void *arg_void_ptr)
{
    (void) arg_void_ptr;
    frame_called = true;
    if (frame_ptr->slab_ptr != NULL)
    {
        last_data = *(frame_ptr->slab_ptr);
        slab_pool_free (frame_ptr->slab_ptr);
    }
}

/**
 * @brief Reset and setup slicer
 */
void test_modbus_slicer_setup (void);

static modbus_slicer_config_t slicer_config = {.baudrate         = 115200,
                                               .bit_length       = 11,
                                               .on_frame_func    = test_frame_cb,
                                               .get_time_us_func = test_time_us,
                                               .cb_arg_void_ptr  = NULL};
static modbus_slicer_t slicer;

void test_modbus_slicer_setup (void)
{
    fake_time    = 0;
    frame_called = false;
    (void) memset (&last_data, 0, sizeof (last_data));

    if (is_setup_done == false)
    {
        (void) slab_pool_init ();
        modbus_slicer_init (&slicer, &slicer_config);
        is_setup_done = true;
    }
}

TEST_CASE ("TEST VALID FRAME DETECTION", "[modbus_slicer]")
{
    uint8_t frame_data[] = {0x01, 0x03, 0x00, 0x10, 0x00, 0x02, 0xC5, 0xCE};

    test_modbus_slicer_setup ();

    // Feed data into the slicer
    modbus_slicer_feed (&slicer, frame_data, sizeof (frame_data), test_time_us ());

    // Ensure frame is not called yet (timeout hasn't occurred)
    modbus_slicer_check_timeout (&slicer);
    TEST_ASSERT_FALSE (frame_called);

    // Advance time beyond T3.5
    fake_time = test_time_us () + slicer.t3_5_us + 100;
    modbus_slicer_check_timeout (&slicer);

    TEST_ASSERT_TRUE (frame_called);
    TEST_ASSERT_EQUAL (sizeof (frame_data), last_data.length);
    TEST_ASSERT_EQUAL_HEX8_ARRAY (frame_data, last_data.data, sizeof (frame_data));
}

TEST_CASE ("TEST OVERFLOW HANDLING", "[modbus_slicer]")
{
    uint8_t large_data[SLAB_POOL_MAX_DATA_SIZE + 10];
    (void) memset (large_data, 0xAA, sizeof (large_data));

    test_modbus_slicer_setup ();

    // Feeding data larger than capacity should drop frame
    modbus_slicer_feed (&slicer, large_data, sizeof (large_data), 1000);

    TEST_ASSERT_EQUAL (0, last_data.length);
    TEST_ASSERT_TRUE (frame_called);

    frame_called = false;

    // Ensure no frame is dispatched for overflowed data
    fake_time = test_time_us () + slicer.t3_5_us + 100;
    modbus_slicer_check_timeout (&slicer);
    TEST_ASSERT_FALSE (frame_called);
}

TEST_CASE ("TEST CHUNKED FEEDING", "[modbus_slicer]")
{
    uint8_t part1[] = {0x01, 0x03};
    uint8_t part2[] = {0x00, 0x10, 0x00, 0x02, 0xC5, 0xCE};

    test_modbus_slicer_setup ();

    // Feed in two chunks
    modbus_slicer_feed (&slicer, part1, sizeof (part1), test_time_us ());
    ++fake_time;
    modbus_slicer_feed (&slicer, part2, sizeof (part2), test_time_us ());

    // Advance time
    fake_time = test_time_us () + slicer.t3_5_us + 100;
    modbus_slicer_check_timeout (&slicer);

    TEST_ASSERT_TRUE (frame_called);
    TEST_ASSERT_EQUAL (sizeof (part1) + sizeof (part2), last_data.length);
}
