#pragma once
#include <stdbool.h>
#include <stdint.h>

#define NODE_FRESH_TIMEOUT_US 16000000LL
static inline bool node_is_fresh(bool valid, int64_t received_us, int64_t now_us)
{
    return valid && now_us >= received_us && now_us - received_us < NODE_FRESH_TIMEOUT_US;
}
