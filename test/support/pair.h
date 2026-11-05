#pragma once

#include "tda/core/cmp.h"

#include <stdint.h>

/* ========== type ========== */

/// An element wider than a word. The short name keeps typed macro calls readable.
typedef struct {
    int64_t a;
    int64_t b;
} Pair;

/* ========== predicates ========== */

/// tda_Pred over Pair, reading the first field
static inline bool tda_test_pair_a_is_positive(const void *elem, void *) {
    return ((const Pair *) elem)->a > 0;
}

/// the negation is a separate predicate, for the same reason tda_span_find_if_not
/// does not exist
static inline bool tda_test_pair_a_is_negative(const void *elem, void *) {
    return ((const Pair *) elem)->a < 0;
}

/* ========== equalities ========== */

/// tda_Eq over Pair, reading the first field alone. Pairs equal under it can still differ
/// byte for byte, which tells an _eq_by from an _eq
static inline bool tda_test_pair_eq_a(const void *lhs, const void *rhs, void *) {
    return ((const Pair *) lhs)->a == ((const Pair *) rhs)->a;
}

/* ========== comparators ========== */

/// tda_Cmp over Pair, ordering by the first field alone
static inline int tda_test_pair_cmp_a(const void *lhs, const void *rhs, void *) {
    return tda_cmp_i64(&((const Pair *) lhs)->a, &((const Pair *) rhs)->a, nullptr);
}
