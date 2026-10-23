#include "tda/alloc/arena.h"
#include "tda/alloc/debug.h"
#include "tda/alloc/default.h"
#include "tda/core/rng.h"
#include "tda/ds/vec.h"

#include "support/probe.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ========== probe allocator ==========
 *
 * The parent is the shared probe, so the tests see what reaches it: the guard on top of
 * each size, and frees held back by the quarantine.
 */

static constexpr size_t GUARD = 16;      // mirrors GUARD_SIZE in debug.c
static constexpr size_t QUARANTINE = 64; // mirrors QUARANTINE_LEN

static tda_TestProbe probe;
static tda_Al parent;

void setUp() {
    tda_test_probe_reset(&probe);
    parent = tda_test_probe_full(&probe);
}

void tearDown() {
}

// a fixed 8, so the sizes the tests check are the same on every platform
static tda_Layout bytes(size_t size) {
    return (tda_Layout){.size = size, .align = 8};
}

static bool all_bytes(const void *ptr, size_t n, unsigned char fill) {
    const unsigned char *p = ptr;
    for (size_t i = 0; i < n; ++i) {
        if (p[i] != fill) {
            return false;
        }
    }

    return true;
}

/* ========== lifetime ========== */

static void test_new_takes_two_blocks_and_drop_returns_them() {
    tda_Al *al = tda_al_debug_new(&parent);
    TEST_ASSERT_NOT_NULL(al);
    TEST_ASSERT_EQUAL_size_t(2, probe.live);

    tda_al_debug_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_new_cleans_up_after_a_failing_parent() {
    tda_test_probe_fail_after_next(&probe, 0);
    TEST_ASSERT_NULL(tda_al_debug_new(&parent));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    tda_test_probe_reset(&probe);
    tda_test_probe_fail_after_next(&probe, 1);
    TEST_ASSERT_NULL(tda_al_debug_new(&parent));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_drop_null_is_noop() {
    tda_al_debug_drop(nullptr);
}

/* ========== alloc and dealloc ========== */

static void test_a_new_block_is_filled_and_guarded() {
    tda_Al *al = tda_al_debug_new(&parent);

    unsigned char *p = tda_alloc(al, bytes(24));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(24 + GUARD, probe.last_alloc.size);
    TEST_ASSERT_EQUAL_size_t(8, probe.last_alloc.align);
    TEST_ASSERT_TRUE(all_bytes(p, 24, 0xCD));
    TEST_ASSERT_TRUE(all_bytes(p + 24, GUARD, 0xFD));

    tda_dealloc(al, p, bytes(24));
    tda_al_debug_drop(al);
}

// the block is still the parent's while quarantined, so reading it here is defined
static void test_a_freed_block_is_poisoned_and_held_back() {
    tda_Al *al = tda_al_debug_new(&parent);

    unsigned char *p = tda_alloc(al, bytes(24));
    tda_dealloc(al, p, bytes(24));

    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);
    TEST_ASSERT_TRUE(all_bytes(p, 24 + GUARD, 0xDD));

    // the drop flushes the quarantine; the size that reaches the parent is checked where
    // the quarantine overflows, below
    tda_al_debug_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_the_quarantine_gives_back_the_oldest_when_full() {
    tda_Al *al = tda_al_debug_new(&parent);

    void *first = tda_alloc(al, bytes(8));
    tda_dealloc(al, first, bytes(8));
    for (size_t i = 1; i < QUARANTINE; ++i) {
        tda_dealloc(al, tda_alloc(al, bytes(32)), bytes(32));
    }
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);

    tda_dealloc(al, tda_alloc(al, bytes(32)), bytes(32));
    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(8 + GUARD, probe.last_dealloc.size);

    tda_al_debug_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the block reaches the parent with its own alignment both ways, the guard on its size
static void test_an_over_aligned_block_keeps_its_alignment() {
    tda_Al *al = tda_al_debug_new(&parent);
    const tda_Layout wide = {.size = 24, .align = 256};

    unsigned char *p = tda_alloc(al, wide);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % 256);
    TEST_ASSERT_EQUAL_size_t(24 + GUARD, probe.last_alloc.size);
    TEST_ASSERT_EQUAL_size_t(256, probe.last_alloc.align);

    // the quarantine holds it back until enough later frees push it out
    tda_dealloc(al, p, wide);
    for (size_t i = 0; i < QUARANTINE; ++i) {
        tda_dealloc(al, tda_alloc(al, bytes(8)), bytes(8));
    }
    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(24 + GUARD, probe.last_dealloc.size);
    TEST_ASSERT_EQUAL_size_t(256, probe.last_dealloc.align);

    tda_al_debug_drop(al);
}

static void test_alloc_zeroed_zeroes_the_block() {
    tda_Al *al = tda_al_debug_new(&parent);

    unsigned char *p = tda_alloc_zeroed(al, bytes(24));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(all_bytes(p, 24, 0));
    TEST_ASSERT_TRUE(all_bytes(p + 24, GUARD, 0xFD));

    tda_dealloc(al, p, bytes(24));
    tda_al_debug_drop(al);
}

// a realloc that could grow in place moves anyway, so a pointer kept across it is stale:
// with no resize and no remap, the wrapper's copy is the only way
static void test_realloc_always_moves_and_poisons_the_old_block() {
    tda_Al *al = tda_al_debug_new(&parent);

    unsigned char *p = tda_alloc(al, bytes(4));
    memcpy(p, "abcd", 4);

    unsigned char *q = tda_realloc(al, p, bytes(4), 8);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_NOT_EQUAL(p, q);
    TEST_ASSERT_EQUAL_MEMORY("abcd", q, 4);
    TEST_ASSERT_TRUE(all_bytes(q + 4, 4, 0xCD));
    TEST_ASSERT_TRUE(all_bytes(p, 4, 0xDD));

    tda_dealloc(al, q, bytes(8));
    tda_al_debug_drop(al);
}

static void test_a_refused_parent_records_nothing() {
    tda_Al *al = tda_al_debug_new(&parent);
    tda_test_probe_fail_after_next(&probe, 0);

    TEST_ASSERT_NULL(tda_alloc(al, bytes(16)));

    const tda_AlDebugStats st = tda_al_debug_stats(al);
    TEST_ASSERT_EQUAL_size_t(0, st.live_blocks);
    TEST_ASSERT_EQUAL_UINT64(1, st.requests);

    tda_test_probe_reset(&probe);
    tda_al_debug_drop(al);
}

static void test_a_size_the_guard_would_overflow_is_refused() {
    tda_Al *al = tda_al_debug_new(&parent);
    const size_t calls = probe.alloc_calls;

    TEST_ASSERT_NULL(tda_alloc(al, bytes(SIZE_MAX - 4)));
    TEST_ASSERT_EQUAL_size_t(calls, probe.alloc_calls);

    tda_al_debug_drop(al);
}

// many live blocks freed in a shuffled order: the table grows several times and every
// removal shifts a run back, and a lost entry would end the run with a false report
static void test_the_table_survives_growth_and_any_free_order() {
    tda_Al *al = tda_al_debug_new(tda_al_default());

    enum { N = 2000 };
    static void *blocks[N];
    static size_t order[N];
    for (size_t i = 0; i < N; ++i) {
        blocks[i] = tda_alloc(al, bytes(1 + i % 40));
        TEST_ASSERT_NOT_NULL(blocks[i]);
        order[i] = i;
    }
    TEST_ASSERT_EQUAL_size_t(N, tda_al_debug_stats(al).live_blocks);

    tda_Rng rng = tda_rng_from_seed(7);
    for (size_t i = N; i > 1; --i) {
        const size_t j = tda_rng_idx(&rng, i);
        const size_t tmp = order[i - 1];
        order[i - 1] = order[j];
        order[j] = tmp;
    }
    for (size_t i = 0; i < N; ++i) {
        const size_t k = order[i];
        tda_dealloc(al, blocks[k], bytes(1 + k % 40));
    }

    TEST_ASSERT_EQUAL_size_t(0, tda_al_debug_stats(al).live_blocks);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_debug_stats(al).live_bytes);
    tda_al_debug_drop(al);
}

// an arena reuses nothing, so the quarantine must not confuse it
static void test_over_an_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    tda_Al *al = tda_al_debug_new(arena);
    TEST_ASSERT_NOT_NULL(al);

    void *p = tda_alloc(al, bytes(100));
    TEST_ASSERT_NOT_NULL(p);
    tda_dealloc(al, p, bytes(100));

    tda_al_debug_drop(al);
    tda_al_arena_drop(arena);
}

/* ========== stats ========== */

static void test_stats_count_live_peak_and_requests() {
    tda_Al *al = tda_al_debug_new(&parent);

    void *a = tda_alloc(al, bytes(10));
    void *b = tda_alloc(al, bytes(30));
    tda_dealloc(al, a, bytes(10));
    void *c = tda_alloc(al, bytes(5));

    const tda_AlDebugStats st = tda_al_debug_stats(al);
    TEST_ASSERT_EQUAL_size_t(2, st.live_blocks);
    TEST_ASSERT_EQUAL_size_t(35, st.live_bytes);
    TEST_ASSERT_EQUAL_size_t(40, st.peak_bytes);
    TEST_ASSERT_EQUAL_UINT64(3, st.requests);

    tda_dealloc(al, b, bytes(30));
    tda_dealloc(al, c, bytes(5));
    tda_al_debug_drop(al);
}

static void test_leaks_are_listed_by_serial() {
    tda_Al *al = tda_al_debug_new(&parent);
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    TEST_ASSERT_EQUAL_size_t(0, tda_al_debug_fprint_leaks(al, stream));

    void *a = tda_alloc(al, bytes(12));
    void *b = tda_alloc(al, bytes(7));
    tda_dealloc(al, a, bytes(12));
    TEST_ASSERT_EQUAL_size_t(1, tda_al_debug_fprint_leaks(al, stream));

    char text[512] = {0};
    rewind(stream);
    TEST_ASSERT_TRUE(fread(text, 1, sizeof(text) - 1, stream) > 0);
    TEST_ASSERT_NOT_NULL(strstr(text, "0 leaked, 0 bytes"));
    TEST_ASSERT_NOT_NULL(strstr(text, "block #2, 7 bytes"));
    TEST_ASSERT_NOT_NULL(strstr(text, "1 leaked, 7 bytes"));
    TEST_ASSERT_NULL(strstr(text, "block #1"));

    fclose(stream);
    tda_dealloc(al, b, bytes(7));
    tda_al_debug_drop(al);
}

/* ========== fault injection ========== */

static void test_fail_after_refuses_every_later_request_until_turned_off() {
    tda_Al *al = tda_al_debug_new(&parent);

    tda_al_debug_fail_after(al, 1);
    void *a = tda_alloc(al, bytes(8));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NULL(tda_alloc(al, bytes(8)));
    TEST_ASSERT_NULL(tda_alloc_zeroed(al, bytes(8)));
    TEST_ASSERT_NULL(tda_realloc(al, a, bytes(8), 16));

    tda_dealloc(al, a, bytes(8)); // giving back still works

    tda_al_debug_fail_after(al, SIZE_MAX);
    void *b = tda_alloc(al, bytes(8));
    TEST_ASSERT_NOT_NULL(b);

    tda_dealloc(al, b, bytes(8));
    tda_al_debug_drop(al);
}

// the use the refusal is for: fail each request of an operation in turn and check that
// a refused push leaves the vec as it was and leaks nothing
static void test_fail_after_walks_the_oom_paths_of_a_push() {
    tda_Al *al = tda_al_debug_new(&parent);

    for (size_t n = 0;; ++n) {
        tda_Vec *v = nullptr;
        if (tda_vec_new(TDA_LAYOUT_OF(int32_t), al, &v) != TDA_STATUS_OK) {
            continue;
        }

        tda_al_debug_fail_after(al, n);
        bool all_in = true;
        for (int32_t i = 0; i < 100 && all_in; ++i) {
            const size_t len = tda_vec_len(v);
            if (TDA_VEC_PUSH(int32_t, v, i) != TDA_STATUS_OK) {
                TEST_ASSERT_EQUAL_size_t(len, tda_vec_len(v));
                all_in = false;
            }
        }
        tda_al_debug_fail_after(al, SIZE_MAX);

        tda_vec_drop(v);
        TEST_ASSERT_EQUAL_size_t(0, tda_al_debug_stats(al).live_blocks);
        if (all_in) {
            break;
        }
    }

    tda_al_debug_drop(al);
}

static void test_break_at_zero_is_off() {
    tda_Al *al = tda_al_debug_new(&parent);

    tda_al_debug_break_at(al, 0);
    void *p = tda_alloc(al, bytes(4));
    TEST_ASSERT_NOT_NULL(p);

    tda_dealloc(al, p, bytes(4));
    tda_al_debug_drop(al);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_takes_two_blocks_and_drop_returns_them);
    RUN_TEST(test_new_cleans_up_after_a_failing_parent);
    RUN_TEST(test_drop_null_is_noop);

    RUN_TEST(test_a_new_block_is_filled_and_guarded);
    RUN_TEST(test_a_freed_block_is_poisoned_and_held_back);
    RUN_TEST(test_the_quarantine_gives_back_the_oldest_when_full);
    RUN_TEST(test_an_over_aligned_block_keeps_its_alignment);
    RUN_TEST(test_alloc_zeroed_zeroes_the_block);
    RUN_TEST(test_realloc_always_moves_and_poisons_the_old_block);
    RUN_TEST(test_a_refused_parent_records_nothing);
    RUN_TEST(test_a_size_the_guard_would_overflow_is_refused);
    RUN_TEST(test_the_table_survives_growth_and_any_free_order);
    RUN_TEST(test_over_an_arena);

    RUN_TEST(test_stats_count_live_peak_and_requests);
    RUN_TEST(test_leaks_are_listed_by_serial);

    RUN_TEST(test_fail_after_refuses_every_later_request_until_turned_off);
    RUN_TEST(test_fail_after_walks_the_oom_paths_of_a_push);
    RUN_TEST(test_break_at_zero_is_off);

    return UNITY_END();
}
