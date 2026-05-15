#include "esp_log.h"
#include "rs485_driver.h"
#include "unity.h"

static rs485_driver_t test_drv;

// Config initialization
static rs485_driver_config_t test_drv_config = {.baudrate       = 115200U,
                                                .port           = 1U,
                                                .data_bits      = 8U,
                                                .parity         = 1,
                                                .rx_pin         = 5U,
                                                .rx_buffer_size = 2048U,
                                                .queue_size     = 20U};

TEST_CASE ("RS485 Driver Init/Deinit", "[rs485]")
{
    // Test successful initialization
    TEST_ASSERT_EQUAL (ESP_OK, rs485_driver_init (&test_drv, &test_drv_config));

    // Verify internal resources were allocated
    TEST_ASSERT_NOT_NULL (test_drv.event_queue);
}

TEST_CASE ("RS485 Driver Init Failure Safety", "[rs485]")
{
    rs485_driver_config_t invalid_cfg = test_drv_config;
    invalid_cfg.rx_buffer_size        = 1; // Force a hardware-level error

    // This should return an error because uart_driver_install will fail
    esp_err_t err = rs485_driver_init (&test_drv, &invalid_cfg);
    TEST_ASSERT_NOT_EQUAL (ESP_OK, err);
}
