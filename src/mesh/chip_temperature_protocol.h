#ifndef CHIP_TEMPERATURE_PROTOCOL_H
#define CHIP_TEMPERATURE_PROTOCOL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Optional tail on duty reports: int16, 0.01 C, little endian.
 * INT16_MIN means unavailable. Legacy reports have no tail. */
#define CHIP_TEMPERATURE_LEN 2
#define CHIP_TEMPERATURE_UNKNOWN INT16_MIN

static inline void chip_temperature_encode(uint8_t *out, int16_t value)
{
    out[0] = (uint16_t)value & 0xff;
    out[1] = (uint16_t)value >> 8;
}

static inline bool chip_temperature_decode_tail(const uint8_t *data, size_t len,
                                                size_t base_len, int16_t *value)
{
    if (!data || !value) return false;
    *value = CHIP_TEMPERATURE_UNKNOWN;
    if (len == base_len) return true;
    if (len != base_len + CHIP_TEMPERATURE_LEN) return false;
    uint16_t raw = data[base_len] | ((uint16_t)data[base_len + 1] << 8);
    int32_t signed_raw = raw >= 0x8000 ? (int32_t)raw - 0x10000 : raw;
    if (signed_raw != CHIP_TEMPERATURE_UNKNOWN &&
        (signed_raw < -4000 || signed_raw > 12500)) return false;
    *value = (int16_t)signed_raw;
    return true;
}
#endif
