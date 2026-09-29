#include "tda/algo/heap.h"
#include "tda/algo/permute.h"
#include "tda/algo/sort.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

/* ========== helpers ========== */

// sorts a copy with the libc qsort — an oracle that shares no code with what is tested
static void sorted_copy(int32_t *dst, const int32_t *src, size_t n) {
    memcpy(dst, src, n * sizeof(int32_t));
    qsort(dst, n, sizeof(int32_t), tda_cmp_i32);
}

// a rearranging op must never invent, drop or duplicate an elem
static bool same_elems(const int32_t *a, const int32_t *b, size_t n) {
    int32_t x[64];
    int32_t y[64];
    TEST_ASSERT_TRUE(n <= 64);

    sorted_copy(x, a, n);
    sorted_copy(y, b, n);

    return memcmp(x, y, n * sizeof(int32_t)) == 0;
}

// walks every permutation of {1..n} and hands each to 'check'. next_permutation is a
// data generator here, not an oracle: were it broken the sweep would visit fewer inputs,
// which cannot turn a failing case into a passing one
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
    } while (tda_span_next_permutation(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, n), tda_cmp_i32));

    size_t want = 1;
    for (size_t i = 2; i <= n; ++i) {
        want *= i;
    }
    TEST_ASSERT_EQUAL_size_t(want, seen);
}

/* ========== make_heap ========== */

static void check_make_heap(const int32_t *src, size_t n) {
    int32_t buf[8];
    memcpy(buf, src, n * sizeof(int32_t));

    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, n);
    tda_span_make_heap(s, tda_cmp_i32);

    TEST_ASSERT_TRUE(tda_span_is_heap(tda_span_mut_to_span(s), tda_cmp_i32));
    TEST_ASSERT_TRUE(same_elems(src, buf, n));
}

// every arrangement of five distinct elems, all 120 of them
static void test_make_heap_builds_a_heap_from_any_permutation() {
    for_every_permutation(5, check_make_heap);
}

static void test_make_heap_puts_the_largest_at_the_root() {
    int32_t buf[7] = {3, 1, 4, 1, 5, 9, 2};

    tda_span_make_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7), tda_cmp_i32);

    TEST_ASSERT_EQUAL_INT32(9, buf[0]);
}

static void test_make_heap_of_empty_or_single_is_a_heap() {
    int32_t one = 7;

    tda_span_make_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0), tda_cmp_i32);
    tda_span_make_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, &one, 1), tda_cmp_i32);

    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), tda_cmp_i32));
    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, &one, 1), tda_cmp_i32));
    TEST_ASSERT_EQUAL_INT32(7, one);
}

// equal elems satisfy the property in either direction, so nothing may be lost to them
static void test_make_heap_keeps_duplicates() {
    constexpr int32_t src[6] = {5, 5, 1, 5, 1, 5};
    int32_t buf[6];
    memcpy(buf, src, sizeof(buf));

    tda_span_make_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), tda_cmp_i32);

    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, buf, 6), tda_cmp_i32));
    TEST_ASSERT_TRUE(same_elems(src, buf, 6));
}

// a descending run already satisfies the property at every edge
static void test_make_heap_leaves_a_descending_run_alone() {
    int32_t buf[5] = {5, 4, 3, 2, 1};

    tda_span_make_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), tda_cmp_i32);

    constexpr int32_t want[5] = {5, 4, 3, 2, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

/* ========== push_heap ========== */

static void test_push_heap_lifts_a_new_largest_to_the_root() {
    int32_t buf[5] = {8, 6, 7, 1, 99};

    tda_span_push_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), tda_cmp_i32);

    TEST_ASSERT_EQUAL_INT32(99, buf[0]);
    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, buf, 5), tda_cmp_i32));
}

// the newcomer stops where it belongs; a smaller one never moves at all
static void test_push_heap_leaves_a_smaller_newcomer_in_place() {
    int32_t buf[5] = {8, 6, 7, 1, 2};

    tda_span_push_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), tda_cmp_i32);

    constexpr int32_t want[5] = {8, 6, 7, 1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

// the vec_push + push_heap pairing, one elem at a time
static void test_push_heap_grows_a_heap_one_elem_at_a_time() {
    constexpr int32_t src[8] = {4, 9, 1, 7, 3, 9, 2, 8};
    int32_t buf[8];

    for (size_t len = 1; len <= 8; ++len) {
        buf[len - 1] = src[len - 1];

        tda_span_push_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, len), tda_cmp_i32);

        TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, buf, len), tda_cmp_i32));
        TEST_ASSERT_TRUE(same_elems(src, buf, len));
    }
}

static void test_push_heap_on_a_single_elem_is_a_noop() {
    int32_t one = 7;

    tda_span_push_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, &one, 1), tda_cmp_i32);

    TEST_ASSERT_EQUAL_INT32(7, one);
}

/* ========== pop_heap ========== */

static void test_pop_heap_parks_the_largest_at_the_end() {
    int32_t buf[6] = {9, 8, 5, 7, 1, 2};

    tda_span_pop_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), tda_cmp_i32);

    TEST_ASSERT_EQUAL_INT32(9, buf[5]);
    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, buf, 5), tda_cmp_i32));
}

// the whole span is no longer a heap afterwards — only the part before the parked elem
static void test_pop_heap_drains_in_descending_order() {
    constexpr int32_t src[7] = {3, 1, 4, 1, 5, 9, 2};
    int32_t buf[7];
    memcpy(buf, src, sizeof(buf));

    tda_span_make_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7), tda_cmp_i32);

    int32_t drained[7];
    for (size_t len = 7; len > 0; --len) {
        tda_span_pop_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, len), tda_cmp_i32);
        drained[7 - len] = buf[len - 1];
    }

    constexpr int32_t want[7] = {9, 5, 4, 3, 2, 1, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, drained, 7);
    TEST_ASSERT_TRUE(same_elems(src, drained, 7));
}

static void test_pop_heap_on_a_single_elem_is_a_noop() {
    int32_t one = 7;

    tda_span_pop_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, &one, 1), tda_cmp_i32);

    TEST_ASSERT_EQUAL_INT32(7, one);
}

/* ========== sort_heap ========== */

static void check_sort_heap(const int32_t *src, size_t n) {
    int32_t buf[8];
    memcpy(buf, src, n * sizeof(int32_t));

    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, n);
    tda_span_make_heap(s, tda_cmp_i32);
    tda_span_sort_heap(s, tda_cmp_i32);

    int32_t want[8];
    sorted_copy(want, src, n);
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, n);
}

// make_heap + sort_heap is heapsort, checked against qsort over all 120 permutations
static void test_sort_heap_orders_every_permutation() {
    for_every_permutation(5, check_sort_heap);
}

static void test_sort_heap_orders_duplicates() {
    constexpr int32_t src[8] = {5, 1, 5, 5, 1, 9, 1, 5};
    int32_t buf[8];
    memcpy(buf, src, sizeof(buf));

    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 8);
    tda_span_make_heap(s, tda_cmp_i32);
    tda_span_sort_heap(s, tda_cmp_i32);

    int32_t want[8];
    sorted_copy(want, src, 8);
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 8);
}

static void test_sort_heap_of_empty_or_single_is_a_noop() {
    int32_t one = 7;

    tda_span_sort_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0), tda_cmp_i32);
    tda_span_sort_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, &one, 1), tda_cmp_i32);

    TEST_ASSERT_EQUAL_INT32(7, one);
}

/* ========== is_heap / is_heap_until ========== */

static void test_is_heap_rejects_a_broken_edge() {
    constexpr int32_t good[5] = {9, 8, 5, 7, 1};
    constexpr int32_t bad[5] = {9, 8, 5, 7, 99};

    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, good, 5), tda_cmp_i32));
    TEST_ASSERT_FALSE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, bad, 5), tda_cmp_i32));
}

static void test_is_heap_until_points_at_the_first_offender() {
    // index 3 is a child of index 1, and 9 > 8 breaks that edge
    constexpr int32_t buf[6] = {9, 8, 5, 9, 1, 2};

    TEST_ASSERT_EQUAL_size_t(3, tda_span_is_heap_until(TDA_SPAN_FROM_DATA(int32_t, buf, 6), tda_cmp_i32));
}

// the documented property: whatever the answer, the prefix before it is itself a heap
static void test_is_heap_until_returns_a_heap_prefix() {
    constexpr int32_t buf[7] = {9, 8, 5, 7, 1, 99, 2};

    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 7);
    const size_t until = tda_span_is_heap_until(s, tda_cmp_i32);

    TEST_ASSERT_EQUAL_size_t(5, until);
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_span_sub(s, 0, until), tda_cmp_i32));
    TEST_ASSERT_FALSE(tda_span_is_heap(tda_span_sub(s, 0, until + 1), tda_cmp_i32));
}

static void test_is_heap_accepts_empty_and_single() {
    constexpr int32_t one = 7;

    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_is_heap_until(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), tda_cmp_i32));

    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, &one, 1), tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(1, tda_span_is_heap_until(TDA_SPAN_FROM_DATA(int32_t, &one, 1), tda_cmp_i32));
}

/* ========== through the comparator ========== */

// there is no min_heap family: a descending comparator is the whole mechanism
static void test_descending_comparator_gives_a_min_heap() {
    constexpr int32_t src[7] = {3, 1, 4, 1, 5, 9, 2};
    int32_t buf[7];
    memcpy(buf, src, sizeof(buf));

    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7);
    tda_span_make_heap(s, tda_cmp_desc_i32);

    TEST_ASSERT_EQUAL_INT32(1, buf[0]);
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_span_mut_to_span(s), tda_cmp_desc_i32));
    TEST_ASSERT_TRUE(same_elems(src, buf, 7));

    tda_span_sort_heap(s, tda_cmp_desc_i32);

    constexpr int32_t want[7] = {9, 5, 4, 3, 2, 1, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 7);
}

/* ========== wide elems ========== */

static int cmp_pair_a(const void *a, const void *b) {
    return tda_cmp_i64(&((const Pair *) a)->a, &((const Pair *) b)->a);
}

// A span of equal keys is already a heap at every edge, so a correct sift_down stops on
// the first comparison and nothing moves. Equal keys alone cannot witness that — the
// array would read the same either way — so the payload carries the evidence.
static void test_make_heap_leaves_equal_elems_where_they_are() {
    Pair buf[6] = {{7, 1}, {7, 2}, {7, 3}, {7, 4}, {7, 5}, {7, 6}};

    tda_span_make_heap(TDA_SPAN_FROM_DATA_MUT(Pair, buf, 6), cmp_pair_a);

    for (size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_INT64(7, buf[i].a);
        TEST_ASSERT_EQUAL_INT64((int64_t) i + 1, buf[i].b);
    }
}

// tda_cmp_i64 reads the first field, but the whole elem must travel with it
static void test_heap_moves_wide_elems_whole() {
    Pair buf[5] = {{1, 10}, {5, 50}, {3, 30}, {2, 20}, {4, 40}};

    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(Pair, buf, 5);
    tda_span_make_heap(s, cmp_pair_a);

    TEST_ASSERT_EQUAL_INT64(5, buf[0].a);
    TEST_ASSERT_EQUAL_INT64(50, buf[0].b);

    tda_span_sort_heap(s, cmp_pair_a);

    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT64((int64_t) i + 1, buf[i].a);
        TEST_ASSERT_EQUAL_INT64(((int64_t) i + 1) * 10, buf[i].b);
    }
}

/* ========== scale ========== */

static constexpr size_t SCALE_N = 65536;

static size_t cmp_calls = 0;

static int cmp_i32_counting(const void *a, const void *b) {
    ++cmp_calls;
    return tda_cmp_i32(a, b);
}

// make_heap is the linear one — that is the whole reason it exists next to a loop of
// push_heap, which costs n log n for the same result.
//
// The input has to be ASCENDING for this to mean anything. Measured at this size:
// scattered input costs 1.76n by Floyd and 3.36n by a push_heap loop, so a 4n bound
// would pass both and prove nothing; ascending costs 2.00n against 14.00n, because there
// every newcomer is the new largest and has to climb the whole tree. Descending is worse
// still as a witness — 1.00n either way.
static void test_make_heap_stays_linear() {
    static int32_t buf[SCALE_N];
    for (size_t i = 0; i < SCALE_N; ++i) {
        buf[i] = (int32_t) i;
    }

    cmp_calls = 0;
    tda_span_make_heap(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), cmp_i32_counting);

    TEST_ASSERT_TRUE(tda_span_is_heap(TDA_SPAN_FROM_DATA(int32_t, buf, SCALE_N), tda_cmp_i32));
    TEST_ASSERT_LESS_THAN_size_t(4 * SCALE_N, cmp_calls);
}

static void test_sort_heap_stays_n_log_n() {
    static int32_t buf[SCALE_N];
    for (size_t i = 0; i < SCALE_N; ++i) {
        buf[i] = (int32_t) (i * 2654435761u >> 8);
    }

    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N);
    tda_span_make_heap(s, tda_cmp_i32);

    cmp_calls = 0;
    tda_span_sort_heap(s, cmp_i32_counting);

    TEST_ASSERT_TRUE(tda_span_is_sorted(tda_span_mut_to_span(s), tda_cmp_i32));
    TEST_ASSERT_LESS_THAN_size_t(3 * SCALE_N * 16, cmp_calls);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_make_heap_builds_a_heap_from_any_permutation);
    RUN_TEST(test_make_heap_puts_the_largest_at_the_root);
    RUN_TEST(test_make_heap_of_empty_or_single_is_a_heap);
    RUN_TEST(test_make_heap_keeps_duplicates);
    RUN_TEST(test_make_heap_leaves_a_descending_run_alone);

    RUN_TEST(test_push_heap_lifts_a_new_largest_to_the_root);
    RUN_TEST(test_push_heap_leaves_a_smaller_newcomer_in_place);
    RUN_TEST(test_push_heap_grows_a_heap_one_elem_at_a_time);
    RUN_TEST(test_push_heap_on_a_single_elem_is_a_noop);

    RUN_TEST(test_pop_heap_parks_the_largest_at_the_end);
    RUN_TEST(test_pop_heap_drains_in_descending_order);
    RUN_TEST(test_pop_heap_on_a_single_elem_is_a_noop);

    RUN_TEST(test_sort_heap_orders_every_permutation);
    RUN_TEST(test_sort_heap_orders_duplicates);
    RUN_TEST(test_sort_heap_of_empty_or_single_is_a_noop);

    RUN_TEST(test_is_heap_rejects_a_broken_edge);
    RUN_TEST(test_is_heap_until_points_at_the_first_offender);
    RUN_TEST(test_is_heap_until_returns_a_heap_prefix);
    RUN_TEST(test_is_heap_accepts_empty_and_single);

    RUN_TEST(test_descending_comparator_gives_a_min_heap);
    RUN_TEST(test_make_heap_leaves_equal_elems_where_they_are);
    RUN_TEST(test_heap_moves_wide_elems_whole);

    RUN_TEST(test_make_heap_stays_linear);
    RUN_TEST(test_sort_heap_stays_n_log_n);

    return UNITY_END();
}
