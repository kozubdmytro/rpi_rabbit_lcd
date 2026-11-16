#ifndef MANAGEMENT_PROTOCOL_H
#define MANAGEMENT_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Version-1 management framing only. Admission, replay protection and SDK work
 * belong to the management worker; decoding does not accept an operation. */
#define MANAGEMENT_VERSION 1
#define MANAGEMENT_COMMAND_UUID 0xA009
#define MANAGEMENT_INVENTORY_UUID 0xA00A
#define MANAGEMENT_STATUS_UUID 0xA00B
#define MANAGEMENT_COMMAND_MAX_LEN 20
#define MANAGEMENT_STATUS_LEN 20
#define MANAGEMENT_INVENTORY_HEADER_LEN 8
#define MANAGEMENT_INVENTORY_RECORD_LEN 20
#define MANAGEMENT_CANDIDATE_MAX 10
#define MANAGEMENT_KNOWN_MAX 10
#define MANAGEMENT_INVENTORY_MAX_LEN (MANAGEMENT_INVENTORY_HEADER_LEN + \
    (MANAGEMENT_CANDIDATE_MAX + MANAGEMENT_KNOWN_MAX) * MANAGEMENT_INVENTORY_RECORD_LEN)

enum {
    MANAGEMENT_SCAN_START = 1,
    MANAGEMENT_SCAN_STOP,
    MANAGEMENT_CAPTURE,
    MANAGEMENT_BIND,
    MANAGEMENT_RETRY_CONFIG,
    MANAGEMENT_RESET,
    MANAGEMENT_FORGET,
};

typedef enum {
    MANAGEMENT_DECODE_OK,
    MANAGEMENT_DECODE_INVALID_LENGTH,
    MANAGEMENT_DECODE_UNSUPPORTED,
    MANAGEMENT_DECODE_INVALID_VALUE,
} management_decode_result_t;

typedef struct {
    uint8_t opcode;
    uint16_t request_id;
    union {
        uint8_t scan_seconds;
        uint8_t uuid[16];
        uint16_t address;
    } target;
} management_command_t;

static inline uint16_t management_read_u16(const uint8_t *data)
{
    return data[0] | ((uint16_t)data[1] << 8);
}

static inline void management_write_u16(uint8_t *out, uint16_t value)
{
    out[0] = value & 0xff;
    out[1] = value >> 8;
}

static inline void management_write_u32(uint8_t *out, uint32_t value)
{
    out[0] = value & 0xff;
    out[1] = (value >> 8) & 0xff;
    out[2] = (value >> 16) & 0xff;
    out[3] = value >> 24;
}

/* Leaves out unchanged on rejection. GATT must separately reject prepared
 * writes, nonzero offsets and Write Without Response before calling this. */
static inline management_decode_result_t management_decode_command(
    const uint8_t *data, size_t len, management_command_t *out)
{
    if (!data || !out || len < 4 || len > MANAGEMENT_COMMAND_MAX_LEN)
        return MANAGEMENT_DECODE_INVALID_LENGTH;
    if (data[0] != MANAGEMENT_VERSION) return MANAGEMENT_DECODE_UNSUPPORTED;
    size_t expected;
    switch (data[1]) {
    case MANAGEMENT_SCAN_START: expected = 5; break;
    case MANAGEMENT_SCAN_STOP:
    case MANAGEMENT_CAPTURE: expected = 4; break;
    case MANAGEMENT_BIND: expected = 20; break;
    case MANAGEMENT_RETRY_CONFIG:
    case MANAGEMENT_RESET:
    case MANAGEMENT_FORGET: expected = 6; break;
    default: return MANAGEMENT_DECODE_UNSUPPORTED;
    }
    if (len != expected) return MANAGEMENT_DECODE_INVALID_LENGTH;
    management_command_t command{};
    command.opcode = data[1];
    command.request_id = management_read_u16(data + 2);
    if (!command.request_id) return MANAGEMENT_DECODE_INVALID_VALUE;
    if (command.opcode == MANAGEMENT_SCAN_START) {
        if (data[4] < 10 || data[4] > 60) return MANAGEMENT_DECODE_INVALID_VALUE;
        command.target.scan_seconds = data[4];
    } else if (command.opcode == MANAGEMENT_BIND) {
        memcpy(command.target.uuid, data + 4, 16); /* SDK byte order, opaque. */
    } else if (command.opcode >= MANAGEMENT_RETRY_CONFIG) {
        command.target.address = management_read_u16(data + 4);
        if (!command.target.address || command.target.address > 0x7fff)
            return MANAGEMENT_DECODE_INVALID_VALUE;
    }
    *out = command;
    return MANAGEMENT_DECODE_OK;
}

enum {
    MANAGEMENT_IDLE,
    MANAGEMENT_QUEUED,
    MANAGEMENT_PROVISIONING,
    MANAGEMENT_CONFIGURING,
    MANAGEMENT_RESETTING,
    MANAGEMENT_DELETING,
    MANAGEMENT_COMPLETE,
    MANAGEMENT_FAILED,
};

enum {
    MANAGEMENT_ERROR_NONE,
    MANAGEMENT_ERROR_RADIO,
    MANAGEMENT_ERROR_PROVISIONING,
    MANAGEMENT_ERROR_CONFIGURING,
    MANAGEMENT_ERROR_DEADLINE,
    MANAGEMENT_ERROR_RESET_UNCONFIRMED,
    MANAGEMENT_ERROR_SDK_DELETE,
    MANAGEMENT_ERROR_CLEANUP,
    MANAGEMENT_ERROR_SCAN_CANCELLED,
};

#define MANAGEMENT_FLAG_SCAN_REQUESTED (1u << 0)
#define MANAGEMENT_FLAG_SCAN_READY (1u << 1)
#define MANAGEMENT_FLAG_OVERFLOW (1u << 2)
#define MANAGEMENT_FLAG_RESET_CONFIRMED (1u << 3)
#define MANAGEMENT_FLAG_LOCAL_REMOVED (1u << 4)
#define MANAGEMENT_FLAG_RESET_UNCONFIRMED (1u << 5)
#define MANAGEMENT_FLAG_SDK_OWNED (1u << 6)

typedef struct {
    uint8_t opcode;
    uint16_t request_id;
    uint8_t state;
    uint8_t error;
    uint16_t address;
    uint8_t flags;
    uint8_t scan_seconds_left;
    uint32_t boot_id;
    uint32_t snapshot_id;
} management_status_t;

/* Encode fields explicitly: C struct padding is never part of the wire format. */
static inline void management_encode_status(uint8_t out[MANAGEMENT_STATUS_LEN],
                                             const management_status_t *status)
{
    out[0] = MANAGEMENT_VERSION;
    out[1] = status->opcode;
    management_write_u16(out + 2, status->request_id);
    out[4] = status->state;
    out[5] = status->error;
    management_write_u16(out + 6, status->address);
    out[8] = status->flags & 0x7f;
    out[9] = status->scan_seconds_left;
    management_write_u32(out + 10, status->boot_id);
    management_write_u32(out + 14, status->snapshot_id);
    out[18] = out[19] = 0;
}

#endif
