/**
 * @file modbus_slicer.h
 * @ingroup modbus_analyzer_module
 * @author Abanoub Salah
 * @brief Slicer for Modbus
 *
 * @details Provides bytes slicing according to Modbus framing rule of
 * 3.5 characters interframe silent duration
 */

#ifndef MODBUS_SLICER_H
#define MODBUS_SLICER_H

#include "slab_pool.h"

#include <stdint.h>

/**
 * @brief Modbus slicer error enum
 */
typedef enum {
    MODBUS_SLICER_OK = 0,         /**< Frame OK and ready */
    MODBUS_SLICER_ERROR_CRC,      /**< Frame CRC error */
    MODBUS_SLICER_ERROR_OVERFLOW, /**< Local buffer would overflowed */
    MODBUS_SLICER_ERROR_NO_MEM    /**< No buffers available */
} modbus_slicer_error_t;

/**
 * @brief Modbus slicer state enum
 */
typedef enum {
    MODBUS_SLICER_STATE_IDLE,     /**< Slicer is idle */
    MODBUS_SLICER_STATE_RECEIVING /**< Slicer in the middle of receiving a frame */
} modbus_slicer_state_t;

/**
 * @brief Modbus slicer frame structure
 */
typedef struct {
    slab_pool_t *slab_ptr;       /**< Pointer to slab buffer */
    int64_t timestamp_us;        /**< Frame timestamp */
    modbus_slicer_error_t error; /**< Current frame error */
} modbus_slicer_frame_t;

/**
 * @brief Modbus slicer configuration structure
 */
typedef struct {
    uint32_t baudrate;                  /**< UART baud rate */
    uint8_t bit_length;                 /**< UART frame bit length */
    int64_t (*get_time_us_func) (void); /**< Function pointer to get time in uSeconds */
    void (*on_frame_func) (const modbus_slicer_frame_t *, void *); /**< Callback on frame/error */
    void *cb_arg_void_ptr;                                         /**< Call back arguments */
} modbus_slicer_config_t;

/**
 * @brief Modbus slicer type structure
 */
typedef struct {
    modbus_slicer_config_t *config_ptr; /**< Pointer to slicer configuration */
    slab_pool_t *slab_ptr;              /**< Pointer to slab buffer */
    int64_t start_timestamp_us;         /**< Start of frame timestamp */
    int64_t last_byte_timestamp_us;     /**< Last received byte timestamp */
    int64_t t3_5_us;                    /**< Pre-calculated 3.5 character time in uSeconds */
} modbus_slicer_t;

/**
 * @brief Initialize slicer
 *
 * @details Initialize slicer by calculating 3.5 characters time using
 * standard frame length 11-bits and baud rate used by rs485 driver
 *
 * @param slicer_ptr Pointer to slicer structure
 * @param config_ptr Pointer to slicer configuration structure
 *
 * @note Assumes standard frame length of 11-bits
 *
 * @note Added 20% to 3.5 characters duration for timing practicality
 */
void modbus_slicer_init (modbus_slicer_t *slicer_ptr, modbus_slicer_config_t *config_ptr);

/**
 * @brief Feed data to slicer state-machine
 *
 * @param slicer_ptr Pointer to slicer structure
 * @param data_ptr Pointer to data buffer
 * @param length Data buffer length
 * @param timestamp_us Data timestamp
 *
 * @note A slab gets allocated here and if passed to
 *       callback function successfully it's the consumer
 *       responsibility to free it otherwise it gets freed
 *       later in the same component
 *
 * @note Emits overflow when data is coming but buffer is exhausted
 *
 * @note Emits no memory when there is no available buffers
 */
void modbus_slicer_feed (modbus_slicer_t *slicer_ptr,
                         const uint8_t *data_ptr,
                         size_t length,
                         int64_t timestamp_us);

/**
 * @brief Signal frame timeout
 *
 * @details Slice the frame since 3.5 characters time has passed and
 * call registered callback function
 *
 * @param slicer_ptr Pointer to slicer structure
 *
 * @note Intercharacter timing is not implemented here since
 *       current setup doesn't allow for 1.5 characters timeout
 *
 * @note Frames are dropped if there is no callback registered
 *
 * @note Callback is triggered if there is data present otherwise
 *       it is called when errors happen and slab is reused if present
 */
void modbus_slicer_timeout (modbus_slicer_t *slicer_ptr);

/**
 * @brief Check for frame timeout
 *
 * @details Check for frame timeout by comparing last received byte
 * to current time and timeout if it exceeds 3.5 characters time
 *
 * @param slicer_ptr Pointer to slicer structure
 *
 * @note Frame boundary primary determined using timing from rs485
 *       driver layer as it's timeout threshold is set approximately
 *       3.5 characters time
 */
void modbus_slicer_check_timeout (modbus_slicer_t *slicer_ptr);

#endif
