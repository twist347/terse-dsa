#pragma once

#include "tda/core/cmp.h"

#include <string.h>

/* Stable order: right precedes left only when strictly less, so equals keep their order. */

/// whether 'right' goes before 'left', the elem that came first
[[nodiscard]]
static inline bool tda_right_goes_first(tda_Cmp cmp, void *ctx, const void *left, const void *right) {
    return cmp(right, left, ctx) < 0;
}

/// the equality behind a container's eq and eq_by: a null 'eq' compares the bytes
[[nodiscard]]
static inline bool tda_elem_eq(tda_Eq eq, void *ctx, const void *lhs, const void *rhs, size_t size) {
    return eq ? eq(lhs, rhs, ctx) : memcmp(lhs, rhs, size) == 0;
}
