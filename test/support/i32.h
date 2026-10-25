#pragma once

#include <stdint.h>

/* ========== predicates ========== */

/// tda_Pred over int32_t
static inline bool tda_test_i32_is_even(const void *elem, void *) {
    return *(const int32_t *) elem % 2 == 0;
}

/// tda_Pred over int32_t: whether the elem exceeds the int32_t 'ctx' points to
static inline bool tda_test_i32_greater_than(const void *elem, void *ctx) {
    return *(const int32_t *) elem > *(const int32_t *) ctx;
}

/// tda_Pred over int32_t: whether the elem's bit is set in the uint32_t mask at 'ctx';
/// one mask per subset of a small range
static inline bool tda_test_i32_in_mask(const void *elem, void *ctx) {
    return *(const uint32_t *) ctx >> *(const int32_t *) elem & 1;
}

/* ========== equalities ========== */

/// tda_Eq over int32_t that ignores the sign, so equal elems can differ byte for byte
static inline bool tda_test_i32_eq_abs(const void *lhs, const void *rhs, void *) {
    const int32_t a = *(const int32_t *) lhs, b = *(const int32_t *) rhs;

    return (a < 0 ? -a : a) == (b < 0 ? -b : b);
}
