#ifndef MANAGEMENT_STATE_H
#define MANAGEMENT_STATE_H
#include <stdbool.h>
#include "management_protocol.h"

#define MANAGEMENT_CANDIDATE_US 30000000LL
#define MANAGEMENT_RADIO_US 5000000LL
#define MANAGEMENT_PROVISION_US 120000000LL
#define MANAGEMENT_CONFIG_US 65000000LL
#define MANAGEMENT_RESET_US 30000000LL
#define MANAGEMENT_DELETE_US 10000000LL
#define MANAGEMENT_ATT_BUSY 0x80
#define MANAGEMENT_ATT_NOT_FOUND 0x81
#define MANAGEMENT_ATT_CAPACITY 0x82
#define MANAGEMENT_ATT_NOT_READY 0x83
#define MANAGEMENT_ATT_CONFLICT 0x84

typedef struct {
    bool used;
    uint8_t uuid[16];
    uint8_t ble_address[6];
    uint8_t address_type;
    uint16_t oob;
    int64_t seen_us;
} management_candidate_t;

typedef struct {
    management_status_t status;
    management_command_t command;
    uint8_t request[MANAGEMENT_COMMAND_MAX_LEN];
    size_t request_len;
    bool owned, pending, scanning, scan_ready, overflow;
    bool reset_pending, deletion_pending, cleanup_pending, sdk_deleted;
    int64_t deadline_us, scan_end_us;
    management_candidate_t candidates[MANAGEMENT_CANDIDATE_MAX];
} management_state_t;

/* All callers serialize access. Identical retained requests are idempotent,
 * including terminal results; synchronous stop/capture do not replace them. */
static inline int management_replay(const management_state_t *state,
                                    const uint8_t *data, size_t len, uint16_t id)
{
    if (!state->request_len || id != state->status.request_id) return 0;
    return len == state->request_len && !memcmp(data, state->request, len) ? 1 : -1;
}

static inline void management_begin(management_state_t *state,
                                     const management_command_t *command,
                                     const uint8_t *data, size_t len, int64_t now)
{
    state->command = *command;
    memcpy(state->request, data, len);
    state->request_len = len;
    state->status.opcode = command->opcode;
    state->status.request_id = command->request_id;
    state->status.state = MANAGEMENT_QUEUED;
    state->status.error = MANAGEMENT_ERROR_NONE;
    state->status.address = command->opcode >= MANAGEMENT_RETRY_CONFIG ? command->target.address : 0;
    state->status.flags = 0;
    state->reset_pending = state->deletion_pending = state->cleanup_pending = state->sdk_deleted = false;
    state->owned = state->pending = true;
    state->deadline_us = now + (command->opcode == MANAGEMENT_SCAN_START ?
        MANAGEMENT_RADIO_US : command->opcode == MANAGEMENT_BIND ? MANAGEMENT_PROVISION_US :
        command->opcode == MANAGEMENT_RESET ? MANAGEMENT_RESET_US :
        command->opcode == MANAGEMENT_FORGET ? MANAGEMENT_DELETE_US : MANAGEMENT_CONFIG_US);
    if (command->opcode == MANAGEMENT_SCAN_START) {
        state->scanning = true;
        state->scan_ready = false;
        state->overflow = false;
    }
}

/* A watchdog never releases SDK ownership. The actual callback drain does. */
static inline void management_result(management_state_t *state, uint8_t error)
{
    if (state->status.state == MANAGEMENT_FAILED) return;
    state->status.state = error ? MANAGEMENT_FAILED : MANAGEMENT_COMPLETE;
    state->status.error = error;
}

/* Worker-only transitions. A successful reset result authorizes one SDK local
 * deletion; a successful deletion result authorizes local cache/storage cleanup.
 * Neither ATT acceptance nor a reset timeout authorizes local removal. */
static inline bool management_removal_started(management_state_t *state, int64_t now)
{
    if (!state->owned || !state->pending || state->status.state != MANAGEMENT_QUEUED ||
        (state->command.opcode != MANAGEMENT_RESET && state->command.opcode != MANAGEMENT_FORGET)) return false;
    state->pending = false;
    if (state->command.opcode == MANAGEMENT_RESET) {
        state->status.state = MANAGEMENT_RESETTING;
        state->reset_pending = true;
        state->deadline_us = now + MANAGEMENT_RESET_US;
    } else {
        state->status.state = MANAGEMENT_DELETING;
        state->status.flags |= MANAGEMENT_FLAG_RESET_UNCONFIRMED;
        state->deletion_pending = true;
        state->deadline_us = now + MANAGEMENT_DELETE_US;
    }
    return true;
}

static inline bool management_reset_result(management_state_t *state, uint16_t address,
                                            bool success, int64_t now)
{
    if (!state->owned || !state->reset_pending || state->command.opcode != MANAGEMENT_RESET ||
        state->status.address != address) return false;
    state->reset_pending = false;
    if (state->status.state == MANAGEMENT_FAILED) return false; /* Drain only after a watchdog. */
    if (!success) {
        state->status.flags |= MANAGEMENT_FLAG_RESET_UNCONFIRMED;
        management_result(state, MANAGEMENT_ERROR_RESET_UNCONFIRMED);
        return false;
    }
    state->status.flags |= MANAGEMENT_FLAG_RESET_CONFIRMED;
    state->status.state = MANAGEMENT_DELETING;
    state->deletion_pending = true;
    state->deadline_us = now + MANAGEMENT_DELETE_US;
    return true;
}

static inline bool management_deletion_result(management_state_t *state, uint16_t address, bool success)
{
    if (!state->owned || !state->deletion_pending || state->status.address != address) return false;
    state->deletion_pending = false;
    if (!success) {
        management_result(state, MANAGEMENT_ERROR_SDK_DELETE);
        return false;
    }
    state->sdk_deleted = state->cleanup_pending = true;
    return true; /* Cleanup is still required, including after a deletion watchdog. */
}

static inline bool management_cleanup_result(management_state_t *state, uint16_t address, bool success)
{
    if (!state->owned || !state->cleanup_pending || !state->sdk_deleted ||
        state->status.address != address) return false;
    state->cleanup_pending = false;
    if (success) state->status.flags |= MANAGEMENT_FLAG_LOCAL_REMOVED;
    management_result(state, success ? MANAGEMENT_ERROR_NONE : MANAGEMENT_ERROR_CLEANUP);
    return true;
}

/* Explicit forget retry after SDK deletion: never submit a second deletion.
 * Preserve confirmed reset evidence from the retained target, if available. */
static inline bool management_resume_cleanup(management_state_t *state, uint8_t evidence)
{
    if (!state->owned || state->command.opcode != MANAGEMENT_FORGET || !state->deletion_pending ||
        state->status.state != MANAGEMENT_DELETING) return false;
    state->status.flags = evidence & MANAGEMENT_FLAG_RESET_CONFIRMED;
    if (!state->status.flags) state->status.flags = MANAGEMENT_FLAG_RESET_UNCONFIRMED;
    state->deletion_pending = false;
    state->sdk_deleted = state->cleanup_pending = true;
    return true;
}

static inline void management_stop_scan(management_state_t *state)
{
    state->scanning = state->scan_ready = false;
    if (state->owned && state->command.opcode == MANAGEMENT_SCAN_START) {
        management_result(state, MANAGEMENT_ERROR_SCAN_CANCELLED);
        state->owned = state->pending = false;
    }
}

static inline void management_tick(management_state_t *state, int64_t now, bool radio_on)
{
    if (state->scanning && state->scan_ready && now >= state->scan_end_us)
        management_stop_scan(state);
    if (!state->owned || state->status.state == MANAGEMENT_COMPLETE) return;
    if (state->command.opcode == MANAGEMENT_SCAN_START && state->pending && radio_on && now < state->deadline_us) {
        state->scan_ready = true;
        state->scan_end_us = now + state->command.target.scan_seconds * 1000000LL;
        state->owned = state->pending = false;
        management_result(state, MANAGEMENT_ERROR_NONE);
    } else if (now >= state->deadline_us) {
        bool reset_wait = state->command.opcode == MANAGEMENT_RESET &&
            !(state->status.flags & MANAGEMENT_FLAG_RESET_CONFIRMED);
        if (reset_wait) state->status.flags |= MANAGEMENT_FLAG_RESET_UNCONFIRMED;
        management_result(state, state->command.opcode == MANAGEMENT_SCAN_START ? MANAGEMENT_ERROR_RADIO :
                          reset_wait ? MANAGEMENT_ERROR_RESET_UNCONFIRMED : MANAGEMENT_ERROR_DEADLINE);
        if (state->command.opcode == MANAGEMENT_SCAN_START) {
            state->scanning = state->scan_ready = false;
            state->owned = state->pending = false;
        }
    }
}

static inline bool management_provisioned(management_state_t *state, const uint8_t uuid[16],
                                          uint16_t address, int64_t now)
{
    if (!state->owned || state->command.opcode != MANAGEMENT_BIND ||
        memcmp(state->command.target.uuid, uuid, 16)) return false;
    state->status.address = address;
    if (state->status.state == MANAGEMENT_FAILED) return false;
    state->status.state = MANAGEMENT_CONFIGURING;
    state->deadline_us = now + MANAGEMENT_CONFIG_US;
    return true;
}

static inline void management_release(management_state_t *state, bool sdk_busy)
{
    if (state->owned && !state->pending && !sdk_busy && !state->reset_pending &&
        !state->deletion_pending && !state->cleanup_pending &&
        (state->status.state == MANAGEMENT_COMPLETE || state->status.state == MANAGEMENT_FAILED))
        state->owned = false;
}

static inline bool management_candidate_fresh(const management_candidate_t *candidate, int64_t now)
{
    return candidate->used && now >= candidate->seen_us &&
           now - candidate->seen_us < MANAGEMENT_CANDIDATE_US;
}

static inline bool management_candidate_selected(const management_state_t *state,
                                                  const management_candidate_t *candidate)
{
    return state->owned && state->command.opcode == MANAGEMENT_BIND &&
           !memcmp(state->command.target.uuid, candidate->uuid, 16);
}

static inline management_candidate_t *management_find_candidate(management_state_t *state,
                                                                  const uint8_t uuid[16], int64_t now)
{
    for (size_t i = 0; i < MANAGEMENT_CANDIDATE_MAX; ++i) {
        if (management_candidate_fresh(&state->candidates[i], now) &&
            !memcmp(state->candidates[i].uuid, uuid, 16)) return &state->candidates[i];
    }
    return NULL;
}

static inline void management_observe(management_state_t *state,
                                      const management_candidate_t *candidate, int64_t now)
{
    if (!state->scanning || !state->scan_ready || now >= state->scan_end_us) return;
    management_candidate_t *slot = NULL;
    for (size_t i = 0; i < MANAGEMENT_CANDIDATE_MAX; ++i) {
        management_candidate_t *entry = &state->candidates[i];
        if (entry->used && !memcmp(entry->uuid, candidate->uuid, 16)) { slot = entry; break; }
        if (!slot && !management_candidate_fresh(entry, now) &&
            !management_candidate_selected(state, entry)) slot = entry;
    }
    if (slot) {
        *slot = *candidate;
        slot->used = true;
        slot->seen_us = now;
    } else state->overflow = true;
}

static inline void management_status_bytes(const management_state_t *state, int64_t now,
                                            uint8_t out[MANAGEMENT_STATUS_LEN])
{
    management_status_t status = state->status;
    status.flags = (state->status.flags & (MANAGEMENT_FLAG_RESET_CONFIRMED |
        MANAGEMENT_FLAG_LOCAL_REMOVED | MANAGEMENT_FLAG_RESET_UNCONFIRMED)) |
        (state->scanning ? MANAGEMENT_FLAG_SCAN_REQUESTED : 0) |
        (state->scan_ready ? MANAGEMENT_FLAG_SCAN_READY : 0) |
        (state->overflow ? MANAGEMENT_FLAG_OVERFLOW : 0) |
        (state->owned ? MANAGEMENT_FLAG_SDK_OWNED : 0);
    status.scan_seconds_left = state->scanning && state->scan_ready && now < state->scan_end_us ?
        (state->scan_end_us - now + 999999) / 1000000 : 0;
    management_encode_status(out, &status);
}

/* Frozen buffers are connection-owned; this helper only slices immutable data. */
static inline bool management_read_slice(const uint8_t *snapshot, size_t length, size_t offset,
                                          uint16_t mtu, uint8_t *out, size_t *copied)
{
    if (offset > length || mtu < 2) return false;
    size_t count = length - offset;
    if (count > (size_t)mtu - 1) count = mtu - 1;
    memcpy(out, snapshot + offset, count);
    *copied = count;
    return true;
}
#endif
