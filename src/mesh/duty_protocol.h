#ifndef DUTY_PROTOCOL_H
#define DUTY_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>
#include "sensor_protocol.h"

/* Versioned by new opcodes: legacy sensor messages never authorize sleep.
 * REPORT = little-endian uint32 transaction ID + unchanged sensor payload
 * + optional int16 chip temperature tail (see chip_temperature_protocol.h).
 * ACK/READY = same ID + uint8 kind. Extended reports use Mesh segmentation; ACK/READY stay unsegmented. */
#define DUTY_OP_BME_REPORT SR_MESH_MODEL_OP_3(0x08, SENSOR_COMPANY_ID)
#define DUTY_OP_SOIL_REPORT SR_MESH_MODEL_OP_3(0x09, SENSOR_COMPANY_ID)
#define DUTY_OP_ACK SR_MESH_MODEL_OP_3(0x0a, SENSOR_COMPANY_ID)
#define DUTY_OP_READY SR_MESH_MODEL_OP_3(0x0b, SENSOR_COMPANY_ID)
#define DUTY_BME 1
#define DUTY_SOIL 2
#define DUTY_CONTROL_LEN 5
#define DUTY_SLEEP_US 12000000LL
#define DUTY_LISTEN_EARLY_US 11500000LL
#define DUTY_WINDOW_END_US 13500000LL
#define DUTY_SETTLE_US 500000LL
#define DUTY_ONLINE_TIMEOUT_US 20000000LL

static inline uint32_t duty_id(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline void duty_put_id(uint8_t *p, uint32_t id)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(id >> (8 * i));
}

/* Pure scheduling state shared with the host tests. Caller supplies a lock. */
typedef struct {
    bool seen;
    bool armed;
    uint8_t kind; /* zero = legacy node, which requires continuous listening */
    uint32_t id;
    int64_t report_us;
    int64_t ready_us;
} duty_peer_t;

static inline void duty_report(duty_peer_t *p, uint32_t id, uint8_t kind, int64_t now)
{
    /* A retransmitted report after READY must not move its wake deadline. */
    if (p->seen && p->kind == kind && p->id == id && kind) return;
    *p = duty_peer_t{};
    p->seen = true;
    p->kind = kind;
    p->id = id;
    p->report_us = now;
}

static inline bool duty_ready(duty_peer_t *p, uint32_t id, uint8_t kind, int64_t now)
{
    if (!p->seen || !kind || p->kind != kind || p->id != id || p->armed ||
        now < p->report_us || now - p->report_us > 3000000) return false;
    p->armed = true;
    p->ready_us = now;
    return true;
}

static inline bool duty_can_pause(const duty_peer_t *p, int64_t now)
{
    /* Once due, stay awake indefinitely until a NEW report/READY handshake.
     * Do not advance a periodic timer blindly after packet loss. */
    return p->seen && p->armed && now >= p->ready_us + DUTY_SETTLE_US &&
           now < p->ready_us + DUTY_LISTEN_EARLY_US;
}
#endif
