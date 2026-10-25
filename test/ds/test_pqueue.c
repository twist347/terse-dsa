#include "tda/ds/pqueue.h"
#include "tda/algo/heap.h"
#include "tda/algo/permute.h"
#include "tda/algo/sort.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/print.h"
#include "tda/ds/vec.h"

#include "support/arena.h"
#include "support/pair.h"
#include "support/probe.h"
#include "support/status.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

/* ========== helpers ========== */

static void push_int(tda_PQueue *q, int32_t val) {
    TDA_TEST_OK(tda_pqueue_push(q, &val));
}

// max-queue over the default allocator, filled one push at a time
static tda_PQueue make_queue(const int32_t *src, size_t n) {
    tda_PQueue q = {};
    TDA_PQUEUE_INIT(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &q);

    for (size_t i = 0; i < n; ++i) {
        push_int(&q, src[i]);
    }

    return q;
}

// the same elems, heapified at once
static tda_PQueue make_queue_from(const int32_t *src, size_t n) {
    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_FROM_DATA(int32_t, src, n, tda_cmp_i32, nullptr, tda_al_default(), &q));

    return q;
}

// the expected output: a copy sorted greatest first by an insertion sort written out
// here, an oracle sharing no code with what is tested
static void expected_order(int32_t *dst, const int32_t *src, size_t n, tda_Cmp cmp, void *ctx) {
    memcpy(dst, src, n * sizeof(int32_t));
    for (size_t i = 1; i < n; ++i) {
        for (size_t j = i; j > 0 && cmp(&dst[j - 1], &dst[j], ctx) > 0; --j) {
            const int32_t tmp = dst[j - 1];
            dst[j - 1] = dst[j];
            dst[j] = tmp;
        }
    }
}

// empties the queue through top + pop, checking after every step that the rest is still
// a heap and one elem shorter. Checked here because it must hold after every pop, not
// just the last
static void assert_drains(tda_PQueue *q, const int32_t *want, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, tda_pqueue_len(q));

    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_EQUAL_INT32(want[i], *TDA_PQUEUE_TOP_AS(int32_t, q));
        tda_pqueue_pop(q);

        TEST_ASSERT_EQUAL_size_t(n - i - 1, tda_pqueue_len(q));
        TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(q), tda_pqueue_cmp(q), nullptr));
    }
}

static void assert_drains_sorted(tda_PQueue *q, const int32_t *src, size_t n, tda_Cmp cmp, void *ctx) {
    int32_t want[64];
    TEST_ASSERT_TRUE(n <= 64);

    expected_order(want, src, n, cmp, ctx);
    assert_drains(q, want, n);
}

// calls 'check' on every permutation of {1..n}. next_permutation only generates data:
// if it were broken the sweep would visit fewer inputs, which cannot hide a failure
static void for_every_permutation(size_t n, void (*check)(const int32_t *, size_t)) {
    int32_t buf[8];
    TEST_ASSERT_TRUE(n <= 8);

    for (size_t i = 0; i < n; ++i) {
        buf[i] = (int32_t) i + 1;
    }

    size_t seen = 0;
    do {
        check(buf, n);
        ++seen;
    } while (tda_span_next_permutation(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, n), tda_cmp_i32, nullptr));

    size_t want = 1;
    for (size_t i = 2; i <= n; ++i) {
        want *= i;
    }
    TEST_ASSERT_EQUAL_size_t(want, seen);
}

// a spread out input with duplicates: not sorted, reversed or unique
static constexpr int32_t SPREAD[] = {5, 1, 9, 9, 3, 7, 2, 8, 3, 6, 0, 4, 9, 1};
static constexpr size_t SPREAD_LEN = sizeof(SPREAD) / sizeof(SPREAD[0]);

/* ========== lifetime ========== */

static void test_init_starts_empty_and_keeps_the_comparator() {
    tda_PQueue q = {};
    TDA_PQUEUE_INIT(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &q);

    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&q));
    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_cap(&q));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_pqueue_elem_layout(&q).size);
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_pqueue_al(&q));
    TEST_ASSERT_EQUAL_PTR(tda_cmp_i32, tda_pqueue_cmp(&q));

    tda_pqueue_deinit(&q);
}

// cap is room, not content
static void test_init_cap_reserves_without_length() {
    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_INIT_CAP(int32_t, 8, tda_cmp_i32, nullptr, tda_al_default(), &q));

    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&q));
    TEST_ASSERT_EQUAL_size_t(8, tda_pqueue_cap(&q));

    tda_pqueue_deinit(&q);
}

static void test_from_data_heapifies_what_it_is_given() {
    tda_PQueue q = make_queue_from(SPREAD, SPREAD_LEN);

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_len(&q));
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&q), tda_cmp_i32, nullptr));

    tda_pqueue_deinit(&q);
}

static void test_from_data_empty_stays_empty() {
    tda_PQueue q = make_queue_from(nullptr, 0);

    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&q));
    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_cap(&q));

    tda_pqueue_deinit(&q);
}

// the queue owns its elems: writing over the source afterwards must not reach them
static void test_from_span_copies_the_view() {
    int32_t src[] = {3, 1, 2};
    tda_PQueue q = {};
    TDA_TEST_OK(
        tda_pqueue_from_span(
            TDA_SPAN_FROM_DATA(int32_t, src, 3),
            tda_cmp_i32,
            nullptr,
            tda_al_default(),
            &q
        )
    );

    src[0] = 100;
    src[1] = 200;
    src[2] = 300;

    constexpr int32_t want[] = {3, 2, 1};
    assert_drains(&q, want, 3);

    tda_pqueue_deinit(&q);
}

// an elem wider than a word, to catch a copy with the wrong stride
static void test_from_data_copies_whole_elems() {
    constexpr Pair src[] = {{.a = 1, .b = 10}, {.a = 3, .b = 30}, {.a = 2, .b = 20}};
    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_FROM_DATA(Pair, src, 3, tda_test_pair_cmp_a, nullptr, tda_al_default(), &q));

    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_pqueue_elem_layout(&q).size);

    const Pair *top = TDA_PQUEUE_TOP_AS(Pair, &q);
    TEST_ASSERT_EQUAL_INT64(3, top->a);
    TEST_ASSERT_EQUAL_INT64(30, top->b);

    tda_pqueue_deinit(&q);
}

static void test_deinit_of_an_inert_value_is_noop() {
    tda_PQueue x = {};
    tda_pqueue_deinit(&x);
    tda_pqueue_deinit(&x);
}

/* ========== order ========== */

static void test_pushes_drain_greatest_first() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);

    assert_drains_sorted(&q, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&q);
}

// every length up to the spread, so growth across a reallocation is covered
static void test_pushes_drain_greatest_first_at_every_length() {
    for (size_t n = 0; n <= SPREAD_LEN; ++n) {
        tda_PQueue q = make_queue(SPREAD, n);
        assert_drains_sorted(&q, SPREAD, n, tda_cmp_desc_i32, nullptr);
        tda_pqueue_deinit(&q);
    }
}

static void check_drains_in_order(const int32_t *src, size_t n) {
    tda_PQueue pushed = make_queue(src, n);
    assert_drains_sorted(&pushed, src, n, tda_cmp_desc_i32, nullptr);
    tda_pqueue_deinit(&pushed);

    // both paths must agree: heapifying is an optimisation, not another order
    tda_PQueue heapified = make_queue_from(src, n);
    assert_drains_sorted(&heapified, src, n, tda_cmp_desc_i32, nullptr);
    tda_pqueue_deinit(&heapified);
}

// every arrangement of five distinct elems, all 120 of them
static void test_every_permutation_drains_in_order() {
    for_every_permutation(5, check_drains_in_order);
}

// there is no min-queue type: a descending comparator is enough
static void test_a_descending_comparator_gives_a_min_queue() {
    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_desc_i32, nullptr, tda_al_default(), &q, 5, 3, 9, 1, 7));

    assert_drains_sorted(&q, (const int32_t[]){5, 3, 9, 1, 7}, 5, tda_cmp_i32, nullptr);

    tda_pqueue_deinit(&q);
}

static void test_top_is_the_greatest_after_every_push() {
    tda_PQueue q = {};
    TDA_PQUEUE_INIT(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &q);

    int32_t high = SPREAD[0];
    for (size_t i = 0; i < SPREAD_LEN; ++i) {
        push_int(&q, SPREAD[i]);
        if (SPREAD[i] > high) {
            high = SPREAD[i];
        }
        TEST_ASSERT_EQUAL_INT32(high, *TDA_PQUEUE_TOP_AS(int32_t, &q));
        TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&q), tda_cmp_i32, nullptr));
    }

    tda_pqueue_deinit(&q);
}

// a const pointer reads through the same typed macros: _Generic picks the const helper
static void test_typed_reads_through_a_const_pointer() {
    tda_PQueue q;
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &q, 2, 9, 4));
    const tda_PQueue *view = &q;

    TEST_ASSERT_EQUAL_INT32(9, *TDA_PQUEUE_TOP_AS(int32_t, view));

    tda_pqueue_deinit(&q);
}

// popping to empty and refilling: the queue must be reusable
static void test_pushes_after_a_full_drain_are_ordered_again() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);

    for (size_t i = 0; i < SPREAD_LEN; ++i) {
        tda_pqueue_pop(&q);
    }
    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&q));

    constexpr int32_t again[] = {2, 8, 5};
    for (size_t i = 0; i < 3; ++i) {
        push_int(&q, again[i]);
    }
    assert_drains_sorted(&q, again, 3, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&q);
}

// SPREAD holds 9 three times and 1 and 3 twice: duplicates are kept, not merged
static void test_duplicates_all_come_back() {
    tda_PQueue q = make_queue_from(SPREAD, SPREAD_LEN);

    size_t nines = 0;
    while (tda_pqueue_len(&q) > 0 && *TDA_PQUEUE_TOP_AS(int32_t, &q) == 9) {
        ++nines;
        tda_pqueue_pop(&q);
    }
    TEST_ASSERT_EQUAL_size_t(3, nines);
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN - 3, tda_pqueue_len(&q));

    tda_pqueue_deinit(&q);
}

// Equal keys come out in no promised order, but every payload must survive exactly
// once. Distinct int32_t elems cannot show this: with equal keys the elems are
// indistinguishable, so a shuffle, a loss and a duplicate all look the same
static void test_equal_keys_keep_every_payload() {
    constexpr Pair src[] = {
        {.a = 7, .b = 1}, {.a = 7, .b = 2}, {.a = 7, .b = 3},
        {.a = 7, .b = 4}, {.a = 7, .b = 5}, {.a = 7, .b = 6},
    };
    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_FROM_DATA(Pair, src, 6, tda_test_pair_cmp_a, nullptr, tda_al_default(), &q));

    bool seen[7] = {};
    for (size_t i = 0; i < 6; ++i) {
        const Pair *top = TDA_PQUEUE_TOP_AS(Pair, &q);
        TEST_ASSERT_EQUAL_INT64(7, top->a);
        TEST_ASSERT_TRUE(top->b >= 1 && top->b <= 6);
        TEST_ASSERT_FALSE_MESSAGE(seen[top->b], "a payload came back twice");
        seen[top->b] = true;
        tda_pqueue_pop(&q);
    }

    tda_pqueue_deinit(&q);
}

// pop_to hands out what top + pop would have read, and leaves a heap after each one
static void test_pop_to_drains_greatest_first() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);

    int32_t want[SPREAD_LEN];
    expected_order(want, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    for (size_t i = 0; i < SPREAD_LEN; ++i) {
        int32_t out = -1;
        tda_pqueue_pop_to(&q, &out);

        TEST_ASSERT_EQUAL_INT32(want[i], out);
        TEST_ASSERT_EQUAL_size_t(SPREAD_LEN - i - 1, tda_pqueue_len(&q));
        TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&q), tda_pqueue_cmp(&q), nullptr));
    }

    tda_pqueue_deinit(&q);
}

static void test_pop_to_macro_moves_whole_elems() {
    tda_PQueue q = {};
    TDA_PQUEUE_INIT(Pair, tda_test_pair_cmp_a, nullptr, tda_al_default(), &q);
    TDA_TEST_OK(tda_pqueue_push(&q, &(Pair){1, 10}));
    TDA_TEST_OK(tda_pqueue_push(&q, &(Pair){3, 30}));
    TDA_TEST_OK(tda_pqueue_push(&q, &(Pair){2, 20}));

    Pair out = {0, 0};
    TDA_PQUEUE_POP_TO(Pair, &q, &out);

    TEST_ASSERT_EQUAL_INT64(3, out.a);
    TEST_ASSERT_EQUAL_INT64(30, out.b);
    TEST_ASSERT_EQUAL_size_t(2, tda_pqueue_len(&q));

    tda_pqueue_deinit(&q);
}

// replacing the top must leave what dropping it and pushing 'val' would: the same
// multiset, as a heap. 'val' ranges from below the least to above the greatest
static void check_replace_top(const int32_t *src, size_t n) {
    for (int32_t val = 0; val <= (int32_t) n + 1; ++val) {
        tda_PQueue q = make_queue_from(src, n);

        tda_pqueue_replace_top(&q, &val);
        TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&q), tda_pqueue_cmp(&q), nullptr));

        // src is a permutation of 1..n, so the dropped top is n
        int32_t after[8];
        size_t m = 0;
        for (size_t i = 0; i < n; ++i) {
            if (src[i] != (int32_t) n) {
                after[m++] = src[i];
            }
        }
        after[m++] = val;

        assert_drains_sorted(&q, after, m, tda_cmp_desc_i32, nullptr);
        tda_pqueue_deinit(&q);
    }
}

static void test_replace_top_matches_a_pop_and_a_push() {
    for (size_t n = 1; n <= 6; ++n) {
        for_every_permutation(n, check_replace_top);
    }
}

// one sift and a write over the root: the queue never allocates
static void test_replace_top_allocates_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_FROM_DATA(int32_t, SPREAD, SPREAD_LEN, tda_cmp_i32, nullptr, &al, &q));
    const size_t before = tda_test_probe_requests(&probe);

    for (int32_t i = 0; i < 32; ++i) {
        TDA_PQUEUE_REPLACE_TOP(int32_t, &q, i);
    }

    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_len(&q));

    tda_pqueue_deinit(&q);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// 'val' may be the top itself, which writes the root over itself and sifts nothing
static void test_replace_top_with_the_top_itself_changes_nothing() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);

    tda_pqueue_replace_top(&q, tda_pqueue_top(&q));

    assert_drains_sorted(&q, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&q);
}

// the k greatest of a stream, kept in a min-queue of k: the use case for replace_top
static void test_replace_top_keeps_the_k_greatest() {
    constexpr size_t k = 4;

    tda_PQueue least_first = {};
    TDA_PQUEUE_INIT(int32_t, tda_cmp_desc_i32, nullptr, tda_al_default(), &least_first);

    for (size_t i = 0; i < SPREAD_LEN; ++i) {
        if (tda_pqueue_len(&least_first) < k) {
            TDA_TEST_OK(tda_pqueue_push(&least_first, &SPREAD[i]));
        } else if (SPREAD[i] > *TDA_PQUEUE_TOP_AS(int32_t, &least_first)) {
            tda_pqueue_replace_top(&least_first, &SPREAD[i]);
        }
    }

    assert_drains(&least_first, (const int32_t[]){8, 9, 9, 9}, k);

    tda_pqueue_deinit(&least_first);
}

/* ========== access ========== */

static void test_top_reads_without_removing() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);

    TEST_ASSERT_EQUAL_INT32(9, *TDA_PQUEUE_TOP_AS(int32_t, &q));
    TEST_ASSERT_EQUAL_INT32(9, *TDA_PQUEUE_TOP_AS(int32_t, &q));
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_len(&q));

    tda_pqueue_deinit(&q);
}

/* ========== info ========== */

static void test_len_follows_push_and_pop() {
    tda_PQueue q = {};
    TDA_PQUEUE_INIT(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &q);

    for (size_t i = 0; i < 5; ++i) {
        push_int(&q, (int32_t) i);
        TEST_ASSERT_EQUAL_size_t(i + 1, tda_pqueue_len(&q));
    }
    for (size_t i = 5; i > 0; --i) {
        TEST_ASSERT_EQUAL_size_t(i, tda_pqueue_len(&q));
        tda_pqueue_pop(&q);
    }
    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&q));

    tda_pqueue_deinit(&q);
}

// a pop frees no memory, just as in the vec underneath
static void test_pop_leaves_the_capacity_alone() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);
    const size_t cap = tda_pqueue_cap(&q);

    tda_pqueue_pop(&q);

    TEST_ASSERT_EQUAL_size_t(cap, tda_pqueue_cap(&q));

    tda_pqueue_deinit(&q);
}

/* ========== mods ========== */

static void test_clear_empties_without_giving_back_the_room() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);
    const size_t cap = tda_pqueue_cap(&q);

    tda_pqueue_clear(&q);

    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&q));
    TEST_ASSERT_EQUAL_size_t(cap, tda_pqueue_cap(&q));

    push_int(&q, 42);
    TEST_ASSERT_EQUAL_INT32(42, *TDA_PQUEUE_TOP_AS(int32_t, &q));

    tda_pqueue_deinit(&q);
}

static void test_reserve_grows_the_room_only() {
    tda_PQueue q = make_queue(SPREAD, 3);

    TDA_TEST_OK(tda_pqueue_reserve(&q, 100));

    TEST_ASSERT_TRUE(tda_pqueue_cap(&q) >= 100);
    TEST_ASSERT_EQUAL_size_t(3, tda_pqueue_len(&q));
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&q), tda_cmp_i32, nullptr));

    tda_pqueue_deinit(&q);
}

// the buffer moves, and the heap must survive the move
static void test_shrink_to_fit_keeps_the_order() {
    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_INIT_CAP(int32_t, 64, tda_cmp_i32, nullptr, tda_al_default(), &q));
    for (size_t i = 0; i < SPREAD_LEN; ++i) {
        push_int(&q, SPREAD[i]);
    }

    TDA_TEST_OK(tda_pqueue_shrink_to_fit(&q));

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_cap(&q));
    assert_drains_sorted(&q, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&q);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_PQueue src = make_queue(SPREAD, SPREAD_LEN), dst = {};
    TDA_TEST_OK(tda_pqueue_copy(&src, &dst));

    push_int(&dst, 1000);
    tda_pqueue_pop(&src);

    TEST_ASSERT_EQUAL_INT32(1000, *TDA_PQUEUE_TOP_AS(int32_t, &dst));
    TEST_ASSERT_EQUAL_INT32(9, *TDA_PQUEUE_TOP_AS(int32_t, &src));
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN + 1, tda_pqueue_len(&dst));
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN - 1, tda_pqueue_len(&src));

    tda_pqueue_deinit(&src);
    tda_pqueue_deinit(&dst);
}

static void test_copy_inherits_the_allocator_and_the_comparator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 1024, &arena));

    tda_PQueue src = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_desc_i32, nullptr, TDA_AL(&arena), &src, 5, 1, 3));

    tda_PQueue dst = {};
    TDA_TEST_OK(tda_pqueue_copy(&src, &dst));

    TEST_ASSERT_EQUAL_PTR(TDA_AL(&arena), tda_pqueue_al(&dst));
    TEST_ASSERT_EQUAL_PTR(tda_cmp_desc_i32, tda_pqueue_cmp(&dst));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_PQUEUE_TOP_AS(int32_t, &dst));

    tda_pqueue_deinit(&src);
    tda_pqueue_deinit(&dst);
    tda_arena_deinit(&arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 1024, &arena));

    tda_PQueue src = make_queue_from(SPREAD, SPREAD_LEN);

    tda_PQueue dst = {};
    TDA_TEST_OK(tda_pqueue_copy_with(&src, TDA_AL(&arena), &dst));

    TEST_ASSERT_EQUAL_PTR(TDA_AL(&arena), tda_pqueue_al(&dst));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_pqueue_al(&src));
    TEST_ASSERT_EQUAL_PTR(tda_cmp_i32, tda_pqueue_cmp(&dst));
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_len(&dst));

    // the source is gone and the copy still drains in order: the buffer is its own
    tda_pqueue_deinit(&src);
    assert_drains_sorted(&dst, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&dst);
    tda_arena_deinit(&arena);
}

// the blocks come from the copy's allocator, not the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 1024, &arena));
    tda_test_arena_leave(&arena, 0);

    tda_PQueue src = make_queue_from(SPREAD, SPREAD_LEN);

    tda_PQueue dst = {};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_pqueue_copy_with(&src, TDA_AL(&arena), &dst));
    TEST_ASSERT_NULL(dst.vec_.al_);
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_len(&src));

    tda_pqueue_deinit(&src);
    tda_arena_deinit(&arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_PQueue src = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, &al, &src, 1, 5, 3));

    tda_PQueue dst = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_desc_i32, nullptr, &al, &dst, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    tda_pqueue_move_assign(&dst, &src);

    // nothing was allocated: the vec's block changed hands
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(3, tda_pqueue_len(&dst));
    TEST_ASSERT_EQUAL_INT32(5, *TDA_PQUEUE_TOP_AS(int32_t, &dst));

    // the elems arrive arranged under the source's comparator, so it travels with them
    TEST_ASSERT_EQUAL_PTR(tda_cmp_i32, tda_pqueue_cmp(&dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&src));

    tda_pqueue_deinit(&src);
    tda_pqueue_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the allocator is part of the value: 'dst' takes the elems with the allocator they came
// from
static void test_move_assign_across_allocators_hands_the_allocator_over() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 1024, &arena));

    tda_PQueue src = make_queue_from(SPREAD, SPREAD_LEN);

    tda_PQueue dst = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_desc_i32, nullptr, TDA_AL(&arena), &dst, 9));

    tda_pqueue_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_len(&dst));
    TEST_ASSERT_EQUAL_PTR(tda_cmp_i32, tda_pqueue_cmp(&dst));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_pqueue_al(&dst));
    assert_drains_sorted(&dst, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&src));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_pqueue_al(&src));

    tda_pqueue_deinit(&src);
    tda_pqueue_deinit(&dst);
    tda_arena_deinit(&arena);
}

// nothing is allocated, so an exhausted allocator on either side cannot stop it
static void test_move_assign_needs_no_memory() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 1024, &arena));

    tda_PQueue dst = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_desc_i32, nullptr, TDA_AL(&arena), &dst, 9));
    tda_test_arena_leave(&arena, 0);

    tda_PQueue src = make_queue_from(SPREAD, SPREAD_LEN);

    tda_pqueue_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_len(&dst));
    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&src));

    tda_pqueue_deinit(&src);
    tda_pqueue_deinit(&dst);
    tda_arena_deinit(&arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_PQueue q = make_queue_from(SPREAD, SPREAD_LEN);

    tda_pqueue_move_assign(&q, &q);

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_pqueue_len(&q));
    assert_drains_sorted(&q, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&q);
}

static void test_copy_drains_the_same_as_its_source() {
    tda_PQueue src = make_queue_from(SPREAD, SPREAD_LEN), dst = {};
    TDA_TEST_OK(tda_pqueue_copy(&src, &dst));

    assert_drains_sorted(&dst, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&src);
    tda_pqueue_deinit(&dst);
}

static void test_copy_of_empty_stays_empty() {
    tda_PQueue src = make_queue(nullptr, 0), dst = {};
    TDA_TEST_OK(tda_pqueue_copy(&src, &dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_len(&dst));

    tda_pqueue_deinit(&src);
    tda_pqueue_deinit(&dst);
}

// the comparator is part of the value: a heap means nothing without its order, so the
// target's own comparator is replaced
static void test_copy_assign_hands_over_the_comparator_too() {
    tda_PQueue src = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_desc_i32, nullptr, tda_al_default(), &src, 5, 1, 3));

    tda_PQueue dst = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &dst, 100, 200, 300));

    TDA_TEST_OK(tda_pqueue_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_PTR(tda_cmp_desc_i32, tda_pqueue_cmp(&dst));
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&dst), tda_pqueue_cmp(&dst), nullptr));
    assert_drains_sorted(&dst, (const int32_t[]){5, 1, 3}, 3, tda_cmp_i32, nullptr);

    tda_pqueue_deinit(&src);
    tda_pqueue_deinit(&dst);
}

static void test_copy_assign_of_itself_changes_nothing() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);

    TDA_TEST_OK(tda_pqueue_copy_assign(&q, &q));

    assert_drains_sorted(&q, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&q);
}

/* ========== swap ========== */

static void test_swap_exchanges_the_elems_and_the_comparators() {
    tda_PQueue a = {}, b = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &a, 1, 2, 3));
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_desc_i32, nullptr, tda_al_default(), &b, 10, 20, 30));

    tda_pqueue_swap(&a, &b);

    TEST_ASSERT_EQUAL_PTR(tda_cmp_desc_i32, tda_pqueue_cmp(&a));
    TEST_ASSERT_EQUAL_PTR(tda_cmp_i32, tda_pqueue_cmp(&b));
    TEST_ASSERT_EQUAL_INT32(10, *TDA_PQUEUE_TOP_AS(int32_t, &a));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_PQUEUE_TOP_AS(int32_t, &b));

    // each side is still a heap under the order that came with its elems
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&a), tda_pqueue_cmp(&a), nullptr));
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&b), tda_pqueue_cmp(&b), nullptr));

    tda_pqueue_deinit(&a);
    tda_pqueue_deinit(&b);
}

static void test_swap_of_itself_changes_nothing() {
    tda_PQueue q = make_queue(SPREAD, SPREAD_LEN);

    tda_pqueue_swap(&q, &q);

    assert_drains_sorted(&q, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr);

    tda_pqueue_deinit(&q);
}

/* ========== to span ========== */

// heap order is weaker than sorted order: only the first elem is in its final place.
// The span is the buffer as it stands, not a sorted run and not a copy
static void test_to_span_shows_the_heap_not_a_sorted_run() {
    tda_PQueue q = make_queue_from(SPREAD, SPREAD_LEN);
    const tda_Span s = tda_pqueue_to_span(&q);

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem.size);
    TEST_ASSERT_EQUAL_PTR(tda_pqueue_top(&q), s.data);
    TEST_ASSERT_TRUE(tda_span_is_heap(s, tda_cmp_i32, nullptr));
    TEST_ASSERT_FALSE(tda_span_is_sorted(s, tda_cmp_desc_i32, nullptr));

    tda_pqueue_deinit(&q);
}

static void test_to_span_of_empty_has_no_elems() {
    tda_PQueue q = make_queue(nullptr, 0);

    TEST_ASSERT_EQUAL_size_t(0, tda_pqueue_to_span(&q).len);

    tda_pqueue_deinit(&q);
}

/* ========== failure ========== */

static void test_from_data_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 128, &arena));

    tda_PQueue q = {};
    TDA_TEST_STATUS(
        TDA_STATUS_ERR_NO_MEM,
        TDA_PQUEUE_FROM_DATA(int32_t, SPREAD, 1000, tda_cmp_i32, nullptr, TDA_AL(&arena), &q)
    );
    TEST_ASSERT_NULL(q.vec_.al_);

    tda_arena_deinit(&arena);
}

// a refused push must leave the queue exactly as it was, not a heap with a hole in it
static void test_push_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 256, &arena));

    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, TDA_AL(&arena), &q, 5, 1, 3));
    tda_test_arena_leave(&arena, 0);

    constexpr int32_t val = 100;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_pqueue_push(&q, &val));

    TEST_ASSERT_EQUAL_size_t(3, tda_pqueue_len(&q));
    TEST_ASSERT_EQUAL_INT32(5, *TDA_PQUEUE_TOP_AS(int32_t, &q));
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_pqueue_to_span(&q), tda_cmp_i32, nullptr));

    tda_pqueue_deinit(&q);
    tda_arena_deinit(&arena);
}

static void test_copy_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 256, &arena));

    tda_PQueue src = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, TDA_AL(&arena), &src, 5, 1, 3));
    tda_test_arena_leave(&arena, 0);

    tda_PQueue dst = {};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_pqueue_copy(&src, &dst));
    TEST_ASSERT_NULL(dst.vec_.al_);

    tda_pqueue_deinit(&src);
    tda_arena_deinit(&arena);
}

static void test_reserve_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init(tda_al_default(), 256, &arena));

    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, TDA_AL(&arena), &q, 5, 1, 3));
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_pqueue_reserve(&q, 1000));
    TEST_ASSERT_EQUAL_size_t(3, tda_pqueue_len(&q));

    tda_pqueue_deinit(&q);
    tda_arena_deinit(&arena);
}

/* ========== into ========== */

// the vec was there all along: taking it copies nothing and hands the elems over in
// heap order, which is not sorted order
static void test_into_vec_hands_the_elems_over() {
    constexpr int32_t src[6] = {3, 1, 4, 1, 5, 9};
    tda_PQueue q = make_queue_from(src, 6);

    const void *before = tda_pqueue_to_span(&q).data;
    const size_t cap = tda_pqueue_cap(&q);

    tda_Vec v;

    tda_pqueue_into_vec(&q, &v);

    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));
    TEST_ASSERT_EQUAL_size_t(6, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_vec_al(&v));

    // still a heap, so the greatest is at the front and the rest are not sorted yet
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_vec_to_span(&v), tda_cmp_i32, nullptr));
    TEST_ASSERT_EQUAL_INT32(9, *TDA_VEC_GET_AS(int32_t, &v, 0));

    tda_vec_deinit(&v);
}

// why hand the vec over: sort_heap finishes in place, as heapsort's second half
static void test_into_vec_then_sort_heap() {
    constexpr int32_t src[6] = {3, 1, 4, 1, 5, 9};
    constexpr int32_t want[6] = {1, 1, 3, 4, 5, 9};

    tda_Vec v;

    tda_PQueue q = make_queue_from(src, 6);
    tda_pqueue_into_vec(&q, &v);

    tda_span_sort_heap(tda_vec_to_span_mut(&v), tda_cmp_i32, nullptr);

    for (size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_INT32(want[i], *TDA_VEC_GET_AS(int32_t, &v, i));
    }

    tda_vec_deinit(&v);
}

static void test_into_vec_of_an_empty_queue() {
    constexpr int32_t src[1] = {7};
    tda_Vec v;
    tda_PQueue q = make_queue(src, 0);
    tda_pqueue_into_vec(&q, &v);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

// the adapter owns no block of its own: handing its vec over allocates and frees
// nothing, and leaves the adapter inert
static void test_into_vec_allocates_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_PQueue s;
    TDA_PQUEUE_INIT(int32_t, tda_cmp_i32, nullptr, &al, &s);
    TDA_TEST_OK(TDA_PQUEUE_PUSH(int32_t, &s, 1));
    const size_t requests = tda_test_probe_requests(&probe);
    const size_t live = probe.live;

    tda_Vec inner;
    tda_pqueue_into_vec(&s, &inner);

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_size_t(live, probe.live);
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(&inner));
    TEST_ASSERT_NULL(s.vec_.al_);

    tda_pqueue_deinit(&s);
    tda_vec_deinit(&inner);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== print ========== */

// printers write to a stream; cases read it back via tmpfile, like test/core/test_print.c
static void assert_prints(const char *expected, const tda_PQueue *q) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_pqueue_fprint(q, stream, tda_fprint_i32);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_elem() {
    tda_PQueue q = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &q, 7));

    assert_prints("[7]\n", &q);

    tda_pqueue_deinit(&q);
}

// with more than one elem the order is the heap's, which is unspecified, so a case
// states the output only where there is nothing to order
static void test_fprint_of_an_empty_pqueue() {
    tda_PQueue q = make_queue(nullptr, 0);

    assert_prints("[]\n", &q);

    tda_pqueue_deinit(&q);
}

// the stdout twin cannot be captured portably, so a case only checks that it runs and
// reaches the same printer
static void test_print_writes_to_stdout() {
    constexpr int32_t src[3] = {1, 2, 3};
    tda_PQueue q = make_queue(src, 3);

    tda_pqueue_print(&q, tda_fprint_i32);

    tda_pqueue_deinit(&q);
}

// each side keeps the allocator its elems came from, and each is given back to it
static void test_swap_across_allocators_carries_each_allocator() {
    tda_TestProbe pa, pb;
    tda_test_probe_reset(&pa);
    tda_test_probe_reset(&pb);
    tda_Al al_a = tda_test_probe_full(&pa), al_b = tda_test_probe_full(&pb);

    tda_PQueue a = {}, b = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, &al_a, &a, 1, 2));
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, &al_b, &b, 3, 4, 5));

    tda_pqueue_swap(&a, &b);

    TEST_ASSERT_EQUAL_PTR(&al_b, tda_pqueue_al(&a));
    TEST_ASSERT_EQUAL_PTR(&al_a, tda_pqueue_al(&b));
    TEST_ASSERT_EQUAL_size_t(3, tda_pqueue_len(&a));

    tda_pqueue_deinit(&a);
    TEST_ASSERT_EQUAL_size_t(0, pb.live);
    tda_pqueue_deinit(&b);
    TEST_ASSERT_EQUAL_size_t(0, pa.live);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_PQueue x = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, tda_al_default(), &x, 1, 2));

    tda_pqueue_deinit(&x);
    TEST_ASSERT_NULL(x.vec_.al_);

    tda_pqueue_deinit(&x);
}

// a refused build leaves 'out' untouched and nothing allocated behind
static void test_from_data_reports_a_refused_block() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);
    tda_test_probe_fail_after_next(&probe, 0);

    tda_PQueue x = {};
    TDA_TEST_STATUS(
        TDA_STATUS_ERR_NO_MEM,
        TDA_PQUEUE_FROM_DATA(int32_t, SPREAD, SPREAD_LEN, tda_cmp_i32, nullptr, &al, &x)
    );

    TEST_ASSERT_NULL(x.vec_.al_);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_init_cap_reports_a_refused_block() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);
    tda_test_probe_fail_after_next(&probe, 0);

    tda_PQueue q = {};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_PQUEUE_INIT_CAP(int32_t, 8, tda_cmp_i32, nullptr, &al, &q));

    TEST_ASSERT_NULL(q.vec_.al_);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a refused growth leaves the elems as they were
static void test_push_reports_a_refused_growth() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_PQueue x = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, &al, &x, 1));

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_PQUEUE_PUSH(int32_t, &x, 9));
    TEST_ASSERT_EQUAL_size_t(1, tda_pqueue_len(&x));

    probe.fail_after = SIZE_MAX;
    tda_pqueue_deinit(&x);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a refused copy_assign leaves the target exactly as it was
static void test_copy_assign_reports_a_refused_block_and_changes_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_PQueue small = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, &al, &small, 1));
    tda_PQueue big = {};
    TDA_TEST_OK(
        TDA_PQUEUE_FROM_DATA(int32_t, SPREAD, SPREAD_LEN, tda_cmp_desc_i32, nullptr, tda_al_default(), &big)
    );

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_pqueue_copy_assign(&small, &big));

    TEST_ASSERT_EQUAL_size_t(1, tda_pqueue_len(&small));
    TEST_ASSERT_EQUAL_PTR(tda_cmp_i32, tda_pqueue_cmp(&small));

    probe.fail_after = SIZE_MAX;
    tda_pqueue_deinit(&small);
    tda_pqueue_deinit(&big);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_empty_and_keeps_the_comparator);
    RUN_TEST(test_init_cap_reserves_without_length);
    RUN_TEST(test_from_data_heapifies_what_it_is_given);
    RUN_TEST(test_from_data_empty_stays_empty);
    RUN_TEST(test_from_span_copies_the_view);
    RUN_TEST(test_from_data_copies_whole_elems);
    RUN_TEST(test_deinit_of_an_inert_value_is_noop);
    RUN_TEST(test_deinit_twice_is_noop);

    RUN_TEST(test_pushes_drain_greatest_first);
    RUN_TEST(test_pushes_drain_greatest_first_at_every_length);
    RUN_TEST(test_every_permutation_drains_in_order);
    RUN_TEST(test_a_descending_comparator_gives_a_min_queue);
    RUN_TEST(test_top_is_the_greatest_after_every_push);
    RUN_TEST(test_typed_reads_through_a_const_pointer);
    RUN_TEST(test_pushes_after_a_full_drain_are_ordered_again);
    RUN_TEST(test_duplicates_all_come_back);
    RUN_TEST(test_equal_keys_keep_every_payload);

    RUN_TEST(test_pop_to_drains_greatest_first);
    RUN_TEST(test_pop_to_macro_moves_whole_elems);
    RUN_TEST(test_replace_top_matches_a_pop_and_a_push);
    RUN_TEST(test_replace_top_allocates_nothing);
    RUN_TEST(test_replace_top_with_the_top_itself_changes_nothing);
    RUN_TEST(test_replace_top_keeps_the_k_greatest);

    RUN_TEST(test_top_reads_without_removing);

    RUN_TEST(test_len_follows_push_and_pop);
    RUN_TEST(test_pop_leaves_the_capacity_alone);

    RUN_TEST(test_clear_empties_without_giving_back_the_room);
    RUN_TEST(test_reserve_grows_the_room_only);
    RUN_TEST(test_shrink_to_fit_keeps_the_order);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_inherits_the_allocator_and_the_comparator);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_hands_the_allocator_over);
    RUN_TEST(test_move_assign_needs_no_memory);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_drains_the_same_as_its_source);
    RUN_TEST(test_copy_of_empty_stays_empty);
    RUN_TEST(test_copy_assign_hands_over_the_comparator_too);
    RUN_TEST(test_copy_assign_of_itself_changes_nothing);

    RUN_TEST(test_swap_exchanges_the_elems_and_the_comparators);
    RUN_TEST(test_swap_of_itself_changes_nothing);

    RUN_TEST(test_to_span_shows_the_heap_not_a_sorted_run);
    RUN_TEST(test_to_span_of_empty_has_no_elems);
    RUN_TEST(test_from_data_reports_an_exhausted_arena);
    RUN_TEST(test_push_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_copy_reports_an_exhausted_arena);
    RUN_TEST(test_reserve_reports_an_exhausted_arena);


    RUN_TEST(test_into_vec_hands_the_elems_over);
    RUN_TEST(test_into_vec_then_sort_heap);
    RUN_TEST(test_into_vec_of_an_empty_queue);
    RUN_TEST(test_into_vec_allocates_nothing);

    RUN_TEST(test_fprint_writes_the_elem);
    RUN_TEST(test_fprint_of_an_empty_pqueue);
    RUN_TEST(test_print_writes_to_stdout);

    RUN_TEST(test_swap_across_allocators_carries_each_allocator);

    RUN_TEST(test_from_data_reports_a_refused_block);
    RUN_TEST(test_init_cap_reports_a_refused_block);
    RUN_TEST(test_push_reports_a_refused_growth);
    RUN_TEST(test_copy_assign_reports_a_refused_block_and_changes_nothing);

    return UNITY_END();
}
