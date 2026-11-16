#ifndef MOISTURE_PROTOCOL_H
#define MOISTURE_PROTOCOL_H
#include "sensor_protocol.h"

/* Distinct opcodes prevent soil readings being decoded as BME280 air readings. */
#define MOISTURE_OP_GET SR_MESH_MODEL_OP_3(0x07, SENSOR_COMPANY_ID)
#define MOISTURE_OP_STATUS SR_MESH_MODEL_OP_3(0x05, SENSOR_COMPANY_ID)
#define MOISTURE_OP_ACK SR_MESH_MODEL_OP_3(0x06, SENSOR_COMPANY_ID)
#define MOISTURE_PAYLOAD_LEN 3
#define MOISTURE_PERCENT_UNKNOWN 255

static inline void moisture_encode(uint8_t out[MOISTURE_PAYLOAD_LEN], uint16_t raw, uint8_t percent)
{
    out[0] = raw & 0xff;
    out[1] = raw >> 8;
    out[2] = percent;
}

static inline bool moisture_decode(const uint8_t *data, size_t len, uint16_t *raw, uint8_t *percent)
{
    if (!data || len != MOISTURE_PAYLOAD_LEN || !raw || !percent) return false;
    uint16_t value = data[0] | ((uint16_t)data[1] << 8);
    if (value > 4095 || (data[2] > 100 && data[2] != MOISTURE_PERCENT_UNKNOWN)) return false;
    *raw = value;
    *percent = data[2];
    return true;
}
#endif
