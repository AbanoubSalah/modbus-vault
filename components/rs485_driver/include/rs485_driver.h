/**
 * @file rs485_driver.h
 * @ingroup rs485_driver_module
 * @author Abanoub Salah
 * @brief RS485 passive sniffer (RX-only) driver
 *
 * @details
 * - Does not transmit or control DE/RE lines
 * - Passively listens to bus traffic using UART RX
 * - Provide a task that provide events as they come to provided callback
 */

#ifndef RS485_DRIVER_H
#define RS485_DRIVER_H

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RS485_DRIVER_TASK_NAME            ("RS485_DRV") /**< Task name */
#define RS485_DRIVER_TASK_STACK_DEPTH     (4096U)       /**< Stack depth */
#define RS485_DRIVER_TASK_PRIORITY        (16U)         /**< Priority */
#define RS485_DRIVER_TASK_CPU_AFFINITY    (0U)          /**< CPU affinity */
#define RS485_DRIVER_TASK_BUFFER_SIZE     (256U)        /**< Event buffer size */
#define RS485_DRIVER_TASK_NOTIFY_STOP_BIT (1U << 0U)    /**< Task stop bit */

/**
 * @brief RS485 driver event flags enum
 */
typedef enum {
    RS485_DRIVER_EVENT_FLAG_DATA       = (1U << 0), /**< Data flag */
    RS485_DRIVER_EVENT_FLAG_RX_TIMEOUT = (1U << 1), /**< Rx timeout flag */
    RS485_DRIVER_EVENT_FLAG_OVERFLOW   = (1U << 2), /**< Overflow flag */
    RS485_DRIVER_EVENT_FLAG_PARITY     = (1U << 3)  /**< Parity flag */
} rs485_driver_event_flags_t;

/**
 * @brief RS485 driver event structure
 */
typedef struct {
    uint8_t data[RS485_DRIVER_TASK_BUFFER_SIZE]; /**< Event data */
    size_t length;                               /**< Event data length */
    int64_t timestamp_us;                        /**< Event timestamp */
    uint8_t flags;                               /**< event flags or-ed together if there is more than one */
} rs485_driver_event_t;

/**
 * @brief RS485 driver configuration structure
 */
typedef struct {
    uint32_t baudrate;       /**< UART baud rate */
    uint8_t port;            /**< UART port */
    uint8_t data_bits;       /**< UART data bits */
    uint8_t parity;          /**< UART parity */
    uint8_t stop_bits;       /**< UART stop bits */
    uint8_t rx_pin;          /**< UART receive pin */
    uint32_t rx_buffer_size; /**< Size of bytes count receiver can hold */
    uint32_t queue_size;     /**< Size of events queue can hold */
    void (*on_event_cb) (
        void *, const rs485_driver_event_t *); /**< Pointer to function called when UART event ready */
    void *on_event_cb_arg_void_ptr;            /**< Void pointer to event callback */
} rs485_driver_config_t;

/**
 * @brief RS485 driver type structure
 */
typedef struct {
    const rs485_driver_config_t *config_ptr; /**< Pointer to driver configuration structure */
    QueueHandle_t event_queue;               /**< Events are pushed here on ESP event */
    uint8_t frame_bit_length;                /**< Frame bit length */
} rs485_driver_t;

/**
 * @brief Initialize RS485 driver
 *
 * @param rs485_drv_ptr Pointer to RS485 driver structure
 * @param config_ptr Pointer to RS485 driver configuration structure

 * @return esp_err_t Initialize result
 * @retval ESP_OK Initialize success
 * @retval ESP_FAIL Initialize fail
 * @retval ESP_ERR_INVALID_ARG Provided invalid argument(s)
 * @retval ESP_ERR_NO_MEM No available memory for resource allocation
 *
 * @note
 * - Driver doesn't use Tx pin as it act as a sniffer only
 *     - Pin is set as UART_PIN_NO_CHANGE
 *     - Mode is set to UART_MODE_RS485_APP_CTRL
 *
 * @note UART RX FIFO FULL threshold is set to 128. For typical
 * Modbus 8 to 256 Bytes is typical 128 characters is a practical
 * value in-between and less than driver's event data buffer
 *
 * @note UART RX timeout threshold is set to 4 closest integer
 * to 3.5 characters Inter-frame timeout of practical Modbus transaction
 */
esp_err_t rs485_driver_init (rs485_driver_t *rs485_drv_ptr, const rs485_driver_config_t *config_ptr);

/**
 * @brief Get frame bits count
 *
 * @param rs485_drv_ptr Pointer to RS485 driver structure
 * @return uint8_t Frame bits count
 *
 * @note Only call this function after driver initialization otherwise return
 * is unpredictable
 */
uint8_t rs485_driver_get_bits_count (rs485_driver_t *rs485_drv_ptr);

/**
 * @brief De-initialize RS485 driver
 *
 * @param rs485_drv_ptr Pointer to RS485 driver structure
 */
void rs485_driver_deinit (rs485_driver_t *rs485_drv_ptr);

#endif
