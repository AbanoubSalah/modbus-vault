/**
 * @file rs485_driver.c
 * @ingroup rs485_driver_module
 * @brief Implementation of the rs485 driver
 *
 * @details
 * - Configured as a passive listener, receive only mode
 * - Driver event task wait on UART events passing them to callback function
 */

#include "rs485_driver.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "hal/uart_ll.h"
#include "runtime_tasks.h"

/** Wait on receiver queue timeout in milli-seconds */
#define RS485_DRIVER_WAIT_ON_RX_QUEUE_MS (10U)

/** RS485 Driver TAG name */
static const char *TAG = "RS485_DRIVER";
/** RS485 Driver task handle */
static TaskHandle_t rs485_driver_task_handle = NULL;

/**
 * @brief RS485 Driver task
 *
 * @param parameters_void_ptr Void pointer to parameters
 *
 * @note Call callback function if available on data/error ready
 *
 * @note Old data in queue is dropped in case of overflow
 *
 * @note Driver provides raw byte stream only with 4 characters timeout,
 * so, frame boundaries must be reconstructed at higher layers
 */
static void rs485_driver_task (void *parameters_void_ptr)
{
    rs485_driver_t *rs485_drv_ptr = (rs485_driver_t *) parameters_void_ptr;
    esp_err_t err                 = ESP_OK;
    uint32_t ulNotifiedValue      = 0;

    if ((rs485_drv_ptr == NULL) || (rs485_drv_ptr->event_queue == NULL))
    {
        err = ESP_FAIL;
    }

    if (err == ESP_OK)
    {
        ESP_LOGI (TAG, "Task \"%s\" started", pcTaskGetName (NULL));
        while (true)
        {
            xTaskNotifyWait (0x00, ULONG_MAX, &ulNotifiedValue, 0);

            if ((ulNotifiedValue & RS485_DRIVER_TASK_NOTIFY_STOP_BIT) != 0)
            {
                ESP_LOGI (TAG, "Task \"%s\" stopped", pcTaskGetName (NULL));
                vTaskDelete (NULL);
            }

            uart_event_t event;
            if (xQueueReceive (rs485_drv_ptr->event_queue, (void *) &event,
                               pdMS_TO_TICKS (RS485_DRIVER_WAIT_ON_RX_QUEUE_MS)))
            {
                // Capture time immediately upon waking up
                rs485_driver_event_t eventPacket = {
                    .length = 0, .timestamp_us = esp_timer_get_time (), .flags = 0};
                bool is_handled_event = false;

                switch (event.type)
                {
                case UART_DATA:
                    size_t buffered_size;
                    uart_get_buffered_data_len (rs485_drv_ptr->config_ptr->port, &buffered_size);
                    size_t to_read = (buffered_size < RS485_DRIVER_TASK_BUFFER_SIZE)
                                         ? buffered_size
                                         : RS485_DRIVER_TASK_BUFFER_SIZE;
                    // Read the actual available bytes up to our packet limit
                    eventPacket.length = uart_read_bytes (rs485_drv_ptr->config_ptr->port,
                                                          (void *) eventPacket.data, to_read, 0);
                    if (event.timeout_flag)
                    {
                        // Flag it with timeout
                        eventPacket.flags |= RS485_DRIVER_EVENT_FLAG_RX_TIMEOUT;
                    }
                    // Flag it as data
                    eventPacket.flags |= RS485_DRIVER_EVENT_FLAG_DATA;
                    is_handled_event = true;
                    break;
                case UART_FIFO_OVF:
                case UART_BUFFER_FULL:
                    eventPacket.flags |= (RS485_DRIVER_EVENT_FLAG_OVERFLOW);
                    uart_flush_input (rs485_drv_ptr->config_ptr->port);
                    xQueueReset (rs485_drv_ptr->event_queue);
                    is_handled_event = true;
                    break;
                case UART_PARITY_ERR:
                case UART_FRAME_ERR:
                    eventPacket.flags |= (RS485_DRIVER_EVENT_FLAG_PARITY);
                    is_handled_event = true;
                    break;
                default:
                    break;
                }

                if ((rs485_drv_ptr->config_ptr->on_event_cb != NULL) && (is_handled_event == true))
                {
                    rs485_drv_ptr->config_ptr->on_event_cb (
                        rs485_drv_ptr->config_ptr->on_event_cb_arg_void_ptr, &eventPacket);
                    is_handled_event = false;
                }
            }
        }
    }
}

/**
 * @brief Stop event bus task
 */
static void rs485_driver_stop_task (void)
{
    if (rs485_driver_task_handle != NULL)
    {
        (void) xTaskNotify (rs485_driver_task_handle, RS485_DRIVER_TASK_NOTIFY_STOP_BIT, eSetBits);
    }
}

/** RS485 Driver task configuration structure */
runtime_task_config_t rs485_driver_task_config = {.name        = RS485_DRIVER_TASK_NAME,
                                                  .entry       = rs485_driver_task,
                                                  .arg         = NULL,
                                                  .stack_depth = RS485_DRIVER_TASK_STACK_DEPTH,
                                                  .priority    = RS485_DRIVER_TASK_PRIORITY,
                                                  .core_id     = RS485_DRIVER_TASK_CPU_AFFINITY,
                                                  .handle      = &rs485_driver_task_handle,
                                                  .stop_func   = rs485_driver_stop_task};

esp_err_t rs485_driver_init (rs485_driver_t *rs485_drv_ptr, const rs485_driver_config_t *config_ptr)
{
    esp_err_t err = ESP_OK;
    if ((rs485_drv_ptr == NULL) || (config_ptr == NULL))
    {
        err = ESP_ERR_INVALID_ARG;
    }

    uart_config_t uart_config = {0};
    if (err == ESP_OK)
    {
        uart_config = (uart_config_t){
            .baud_rate           = config_ptr->baudrate,
            .flow_ctrl           = UART_HW_FLOWCTRL_DISABLE,
            .rx_flow_ctrl_thresh = 0,
            .source_clk          = UART_SCLK_APB,
        };

        rs485_drv_ptr->frame_bit_length = 1;

        switch (config_ptr->data_bits)
        {
        case 5:
            rs485_drv_ptr->frame_bit_length += 5;
            uart_config.data_bits = UART_DATA_5_BITS;
            break;
        case 6:
            rs485_drv_ptr->frame_bit_length += 6;
            uart_config.data_bits = UART_DATA_6_BITS;
            break;
        case 7:
            rs485_drv_ptr->frame_bit_length += 7;
            uart_config.data_bits = UART_DATA_7_BITS;
            break;
        case 8:
            rs485_drv_ptr->frame_bit_length += 8;
            uart_config.data_bits = UART_DATA_8_BITS;
            break;
        default:
            rs485_drv_ptr->frame_bit_length += 8;
            uart_config.data_bits = UART_DATA_8_BITS;
            break;
        }

        if (config_ptr->parity == 0)
        {
            uart_config.parity = UART_PARITY_DISABLE;
        }
        else
        {
            uart_config.parity = (uart_parity_t) config_ptr->parity;
            rs485_drv_ptr->frame_bit_length += 1;
        }

        if (config_ptr->stop_bits == 1)
        {
            uart_config.stop_bits = UART_STOP_BITS_1;
            rs485_drv_ptr->frame_bit_length += 1;
        }
        else
        {
            uart_config.stop_bits = UART_STOP_BITS_2;
            rs485_drv_ptr->frame_bit_length += 2;
        }

        rs485_drv_ptr->config_ptr = config_ptr;
        // Install driver (this enables EVENT internally)
        err = uart_driver_install (config_ptr->port,
                                   config_ptr->rx_buffer_size, // RX buffer (EVENT-backed ring buffer)
                                   0,                          // TX buffer not needed
                                   config_ptr->queue_size, &rs485_drv_ptr->event_queue, 0);
    }

    if (err == ESP_OK)
    {
        err = uart_param_config (config_ptr->port, &uart_config);
    }

    if (err == ESP_OK)
    {
        err = uart_set_pin (config_ptr->port, UART_PIN_NO_CHANGE, config_ptr->rx_pin, UART_PIN_NO_CHANGE,
                            UART_PIN_NO_CHANGE);
    }

    if (err == ESP_OK)
    {
        // Set full fifo interrupt
        err = uart_set_rx_full_threshold (config_ptr->port, 128);
    }

    if (err == ESP_OK)
    {
        // Set rx-timeout interrupt
        err = uart_set_rx_timeout (config_ptr->port, 4);
    }

    if (err == ESP_OK)
    {
        // Set to RS-485 Sniffer Mode
        err = uart_set_mode (config_ptr->port, UART_MODE_RS485_APP_CTRL);
    }

    if (err != ESP_OK)
    {
        ESP_LOGW (TAG, "Failed to initialize: (%s).", esp_err_to_name (err));
        (void) rs485_driver_deinit (rs485_drv_ptr);
    }
    else
    {
        rs485_driver_task_config.arg = rs485_drv_ptr;
        ESP_LOGI (TAG, "Initialized");
    }

    return err;
}

uint8_t rs485_driver_get_bits_count (rs485_driver_t *rs485_drv_ptr)
{
    return rs485_drv_ptr->frame_bit_length;
}

void rs485_driver_deinit (rs485_driver_t *rs485_drv_ptr)
{
    if (rs485_drv_ptr != NULL)
    {
        if (rs485_drv_ptr->config_ptr != NULL)
        {
            // Reset and delete hardware driver
            (void) uart_driver_delete (rs485_drv_ptr->config_ptr->port);
        }
        rs485_drv_ptr->event_queue = NULL;
    }

    ESP_LOGI (TAG, "RS485 Driver de-initialized");
}
