#pragma once

#include "tda/alloc/arena.h"

#include <unity.h>

#include <stddef.h>

/* ========== mods ========== */

/// leaves exactly 'want' free bytes in the arena, so a larger allocation fails
/// without a probe allocator
static inline void tda_test_arena_leave(tda_Al *arena, size_t want) {
    const size_t available = tda_al_arena_stats(arena).available;
    TEST_ASSERT_TRUE(available >= want);

    const size_t burn = available - want;
    if (burn > 0) {
        TEST_ASSERT_NOT_NULL(tda_alloc(arena, burn));
    }
    TEST_ASSERT_EQUAL_size_t(want, tda_al_arena_stats(arena).available);
}
