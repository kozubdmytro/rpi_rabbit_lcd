#ifndef SENSOR_PROTOCOL_H
#define SENSOR_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pi-side copy of the ESP32 sensor protocol; see BLE_MIGRATION.md.
 * Preserve the IDF opcode integer representation without importing its SDK.
 * On air: selector | 0xC0, company low byte, company high byte. */
#define SR_MESH_MODEL_OP_3(b0, cid) ((((uint32_t)(b0) << 16) | 0xC00000u) | (uint32_t)(cid))
#define SENSOR_COMPANY_ID 0x1234
#define SENSOR_SERVER_MODEL_ID 0x5678
#define SENSOR_CLIENT_MODEL_ID 0x5679
#define SENSOR_OP_GET SR_MESH_MODEL_OP_3(0x01, SENSOR_COMPANY_ID)
#define SENSOR_OP_STATUS SR_MESH_MODEL_OP_3(0x03, SENSOR_COMPANY_ID)
/* Provisioner echoes an accepted reading to confirm two-way reachability. */
#define SENSOR_OP_ACK SR_MESH_MODEL_OP_3(0x04, SENSOR_COMPANY_ID)
#define SENSOR_ACK_TIMEOUT_US (10LL * 1000 * 1000)
#define SENSOR_PAYLOAD_LEN 4
#define SENSOR_GATEWAY_ADDR 0x0001
#define SENSOR_UUID_PREFIX_0 0xaa
#define SENSOR_UUID_PREFIX_1 0xbb

/* Little endian: signed temperature in 0.01 C, humidity in 0.01 %RH.
 * The Mesh source address identifies the node; no truncated 8-bit node ID.
 */
static inline void sensor_encode(uint8_t out[SENSOR_PAYLOAD_LEN],
                                 int16_t temperature, uint16_t humidity)
{
    out[0] = (uint16_t)temperature & 0xff;
    out[1] = (uint16_t)temperature >> 8;
    out[2] = humidity & 0xff;
    out[3] = humidity >> 8;
}

static inline bool sensor_decode(const uint8_t *data, size_t len,
                                 int16_t *temperature, uint16_t *humidity)
{
    if (!data || len != SENSOR_PAYLOAD_LEN || !temperature || !humidity) {
        return false;
    }
    uint16_t raw = data[0] | ((uint16_t)data[1] << 8);
    int32_t signed_raw = raw >= 0x8000 ? (int32_t)raw - 0x10000 : raw;
    uint16_t h = data[2] | ((uint16_t)data[3] << 8);
    if (signed_raw < -4000 || signed_raw > 8500 || h > 10000) {
        return false;
    }
    *temperature = (int16_t)signed_raw;
    *humidity = h;
    return true;
}

#endif
