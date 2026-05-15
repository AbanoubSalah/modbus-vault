/**
 * @file blackbox_logger_internal.h
 * @ingroup logger_module
 * @author Abanoub Salah
 * @brief Flash-backed circular logging system header internals
 *
 * @details Blackbox logger internal header provides different
 * definitions and structures needed by the logger
 */

#ifndef BLACKBOX_LOGGER_INTERNAL_H
#define BLACKBOX_LOGGER_INTERNAL_H

#include "blackbox_logger.h"

#include <stdint.h>

#define BLACKBOX_LOGGER_HEADER_MAGIC (0x5355424DUL) /**< "MBUS" reversed */
#define BLACKBOX_LOGGER_TAIL_MAGIC   (0x4D425553UL) /**< "SUBM" reversed */
#define BLACKBOX_LOGGER_ENTRY_SIZE_WITHOUT_DATA_LENGTH                                                       \
    (sizeof (blackbox_logger_entry_header_t) +                                                               \
     sizeof (blackbox_logger_entry_tail_t))           /**< Entry size without data field */
#define BLACKBOX_LOGGER_WRITE_LOCK_TIMEOUT_MS  (50U)  /**< Write lock wait timeout in micro seconds */
#define BLACKBOX_LOGGER_SECTOR_LOCK_TIMEOUT_MS (500U) /**< Sector lock wait timeout in micro seconds */

/**
 * @brief Blackbox logger entry header structure
 */
typedef struct {
    uint32_t header_magic; /**< Magic header */
    uint32_t id;           /**< Monotonically increasing ID */
    uint32_t length;       /**< payload length */
} __attribute__ ((packed)) blackbox_logger_entry_header_t;

/**
 * @brief Blackbox logger entry tail structure
 */
typedef struct {
    uint16_t crc;        /**< CRC16 for (header + data) */
    uint32_t tail_magic; /**< Magic tail */
} __attribute__ ((packed)) blackbox_logger_entry_tail_t;

/**
 * @brief Align-up number
 *
 * @details Align number up-to nearest multiple
 * configurable in kconfig
 *
 * @param number Number to align
 * @param align_to Align to nearest
 *
 * @return size_t Aligned number
 */
inline size_t align_num_up_helper (size_t number, size_t align_to)
{
    return ((number + (align_to - 1U)) & (~(align_to - 1U)));
}

/**
 * @brief Calculates entry total size
 *
 * @param data_length Length of data
 *
 * @return size_t Total entry size
 */
inline size_t entry_total_size_helper (size_t data_length)
{
    return (BLACKBOX_LOGGER_ENTRY_SIZE_WITHOUT_DATA_LENGTH + data_length);
}

/**
 * @brief Calculates entry total aligned size
 *
 * @param data_length Length of data
 * @param align_to Align to nearest
 *
 * @return size_t Total entry size
 */
size_t entry_total_aligned_size_helper (size_t data_length, size_t align_to)
{
    return align_num_up_helper (BLACKBOX_LOGGER_ENTRY_SIZE_WITHOUT_DATA_LENGTH + data_length, align_to);
}

/**
 * @brief Check if candidate comes after reference taking wrap-around into account
 *
 * @param candidate Candidate number
 * @param reference Reference number
 * @return bool true if candidate is logically "after" reference
 */
static inline bool is_logically_next_in_order_helper (uint32_t candidate, uint32_t reference)
{
    // Modular arithmetic for sequence numbers
    return (uint32_t) (candidate - reference) < 0x80000000U;
}

#endif
