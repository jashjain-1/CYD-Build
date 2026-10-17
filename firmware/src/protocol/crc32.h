#ifndef DASHCUE_PROTOCOL_CRC32_H
#define DASHCUE_PROTOCOL_CRC32_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Calculate IEEE 802.3 32-bit CRC (polynomial 0xEDB88320).
 * Initial value: 0xFFFFFFFF, Final XOR: 0xFFFFFFFF.
 *
 * @param data Pointer to input byte buffer.
 * @param length Number of bytes to process.
 * @return Computed 32-bit CRC value.
 */
uint32_t crc32_calculate(const uint8_t *data, size_t length);

/**
 * Incrementally update IEEE 802.3 CRC32 calculation.
 *
 * @param current_crc Current CRC result from previous chunk.
 * @param data Pointer to input byte buffer.
 * @param length Number of bytes to process.
 * @return Updated 32-bit CRC value.
 */
uint32_t crc32_update(uint32_t current_crc, const uint8_t *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif // DASHCUE_PROTOCOL_CRC32_H
