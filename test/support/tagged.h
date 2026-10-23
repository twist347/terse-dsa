#pragma once

#include "tda/core/cmp.h"

#include <stdint.h>

/* ========== type ========== */

/// ordered by key only, so tag shows where each of the equal elems went. No tda_test_
/// prefix, for the same reason as Pair
typedef struct {
    int32_t key;
    int32_t tag;
} Tagged;

/* ========== comparators ========== */

/// tda_Cmp over Tagged, reading the key alone
static inline int tda_test_tagged_cmp_key(const void *lhs, const void *rhs, void *) {
    return tda_cmp_i32(&((const Tagged *) lhs)->key, &((const Tagged *) rhs)->key, nullptr);
}
