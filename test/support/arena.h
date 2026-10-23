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
        // align 1, so no padding comes on top of what is burnt
        TEST_ASSERT_NOT_NULL(tda_alloc(arena, (tda_Layout){.size = burn, .align = 1}));
    }
    TEST_ASSERT_EQUAL_size_t(want, tda_al_arena_stats(arena).available);
}
