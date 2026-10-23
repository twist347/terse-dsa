#include "tda/algo/modify.h"
#include "tda/algo/sort.h"

#include "support/i32.h"
#include "support/pair.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// "within one" is deliberately not transitive: 1~2 and 2~3, but 1 !~ 3.
// That tells comparing against the last kept elem apart from comparing
// against the previous one read
static bool eq_within_one(const void *lhs, const void *rhs, void *) {
    const int32_t a = *(const int32_t *) lhs;
    const int32_t b = *(const int32_t *) rhs;
    const int32_t d = a > b ? a - b : b - a;

    return d <= 1;
}

/* ========== unique ========== */

static void test_unique_collapses_runs() {
    int32_t buf[7] = {1, 1, 2, 3, 3, 3, 4};

    const size_t n = tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7), tda_eq_i32, nullptr);

    TEST_ASSERT_EQUAL_size_t(4, n);
    constexpr int32_t want[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 4);
}

// only adjacent elems are compared, so a value coming back later survives
static void test_unique_keeps_non_adjacent_repeats() {
    int32_t buf[4] = {1, 1, 2, 1};

    const size_t n = tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), tda_eq_i32, nullptr);

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[3] = {1, 2, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 3);
}

static void test_unique_of_all_equal_keeps_one() {
    int32_t buf[4] = {7, 7, 7, 7};

    TEST_ASSERT_EQUAL_size_t(1, tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), tda_eq_i32, nullptr));
    TEST_ASSERT_EQUAL_INT32(7, buf[0]);
}

static void test_unique_without_runs_changes_nothing() {
    int32_t buf[4] = {1, 2, 3, 4};

    TEST_ASSERT_EQUAL_size_t(4, tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), tda_eq_i32, nullptr));
    constexpr int32_t want[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 4);
}

static void test_unique_of_short_spans() {
    int32_t one[1] = {5};

    TEST_ASSERT_EQUAL_size_t(0, tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0), tda_eq_i32, nullptr));
    TEST_ASSERT_EQUAL_size_t(1, tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, one, 1), tda_eq_i32, nullptr));
}

static void test_unique_honours_the_callback() {
    int32_t buf[4] = {1, -1, 2, 3};

    const size_t n = tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), tda_test_i32_eq_abs, nullptr);

    TEST_ASSERT_EQUAL_size_t(3, n); // 1 and -1 are equal by absolute value
    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 3);
}

// with a non-transitive equality the two readings diverge: against the last
// kept elem {1,2,3} collapses to {1,3}, against the previous one read to {1}
static void test_unique_compares_against_the_last_kept_elem() {
    int32_t buf[3] = {1, 2, 3};

    const size_t n = tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3), eq_within_one, nullptr);

    TEST_ASSERT_EQUAL_size_t(2, n);
    constexpr int32_t want[2] = {1, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 2);
}

// sorting first turns unique into "make a set"
static void test_sort_then_unique_yields_a_set() {
    int32_t buf[8] = {3, 1, 2, 3, 1, 2, 1, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 8);

    tda_span_sort(s, tda_cmp_i32, nullptr);
    const size_t n = tda_span_unique(s, tda_eq_i32, nullptr);

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 3);
}

static void test_unique_moves_whole_elems() {
    Pair buf[3] = {{1, 10}, {1, 20}, {2, 30}};

    const size_t n = tda_span_unique(TDA_SPAN_FROM_DATA_MUT(Pair, buf, 3), tda_eq_i64, nullptr);

    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_INT64(1, buf[0].a);
    TEST_ASSERT_EQUAL_INT64(10, buf[0].b);
    TEST_ASSERT_EQUAL_INT64(2, buf[1].a);
    TEST_ASSERT_EQUAL_INT64(30, buf[1].b); // b travelled with its a
}

/* ========== remove ========== */

static void test_remove_drops_every_occurrence() {
    int32_t buf[6] = {1, 9, 2, 9, 3, 9};
    constexpr int32_t key = 9;

    const size_t n = tda_span_remove(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), &key, tda_eq_i32, nullptr);

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 3);
}

static void test_remove_of_a_missing_key_changes_nothing() {
    int32_t buf[3] = {1, 2, 3};
    constexpr int32_t key = 9;

    TEST_ASSERT_EQUAL_size_t(3, tda_span_remove(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3), &key, tda_eq_i32, nullptr));
    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 3);
}

static void test_remove_can_empty_the_span() {
    int32_t buf[3] = {9, 9, 9};
    constexpr int32_t key = 9;

    TEST_ASSERT_EQUAL_size_t(0, tda_span_remove(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3), &key, tda_eq_i32, nullptr));
}

static void test_remove_of_an_empty_span_is_zero() {
    constexpr int32_t key = 1;

    TEST_ASSERT_EQUAL_size_t(
        0,
        tda_span_remove(TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0), &key, tda_eq_i32, nullptr)
    );
}

static void test_remove_if_drops_matching_elems() {
    int32_t buf[6] = {1, 2, 3, 4, 5, 6};

    const size_t n = tda_span_remove_if(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), tda_test_i32_is_even, nullptr);

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[3] = {1, 3, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 3);
}

static void test_remove_if_passes_the_ctx_through() {
    int32_t buf[5] = {1, 5, 2, 4, 3};
    int32_t bound = 3;

    const size_t n = tda_span_remove_if(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), tda_test_i32_greater_than, &bound);

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 3);
}

static void test_remove_if_keeps_the_order_of_survivors() {
    int32_t buf[7] = {5, 2, 7, 4, 9, 6, 1};

    const size_t n = tda_span_remove_if(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7), tda_test_i32_is_even, nullptr);

    TEST_ASSERT_EQUAL_size_t(4, n);
    constexpr int32_t want[4] = {5, 7, 9, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 4);
}

static void test_remove_if_moves_whole_elems() {
    Pair buf[4] = {{-1, 10}, {1, 20}, {-2, 30}, {2, 40}};

    const size_t n = tda_span_remove_if(TDA_SPAN_FROM_DATA_MUT(Pair, buf, 4), tda_test_pair_a_is_negative, nullptr);

    TEST_ASSERT_EQUAL_size_t(2, n);
    TEST_ASSERT_EQUAL_INT64(1, buf[0].a);
    TEST_ASSERT_EQUAL_INT64(20, buf[0].b);
    TEST_ASSERT_EQUAL_INT64(2, buf[1].a);
    TEST_ASSERT_EQUAL_INT64(40, buf[1].b);
}

/* ========== replace ========== */

static void test_replace_overwrites_every_occurrence() {
    int32_t buf[5] = {1, 9, 2, 9, 3};
    constexpr int32_t key = 9;
    constexpr int32_t val = 0;

    tda_span_replace(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), &key, &val, tda_eq_i32, nullptr);

    constexpr int32_t want[5] = {1, 0, 2, 0, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

static void test_replace_of_a_missing_key_changes_nothing() {
    int32_t buf[3] = {1, 2, 3};
    constexpr int32_t key = 9;
    constexpr int32_t val = 0;

    tda_span_replace(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3), &key, &val, tda_eq_i32, nullptr);

    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 3);
}

static void test_replace_if_overwrites_matching_elems() {
    int32_t buf[6] = {1, 2, 3, 4, 5, 6};
    constexpr int32_t val = 0;

    tda_span_replace_if(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), tda_test_i32_is_even, nullptr, &val);

    constexpr int32_t want[6] = {1, 0, 3, 0, 5, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 6);
}

static void test_replace_if_passes_the_ctx_through() {
    int32_t buf[5] = {1, 5, 2, 4, 3};
    int32_t bound = 3;
    constexpr int32_t val = 0;

    tda_span_replace_if(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), tda_test_i32_greater_than, &bound, &val);

    constexpr int32_t want[5] = {1, 0, 2, 0, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

static void test_replace_writes_whole_elems() {
    Pair buf[3] = {{1, 10}, {-1, 20}, {2, 30}};
    constexpr Pair val = {7, 70};

    tda_span_replace_if(TDA_SPAN_FROM_DATA_MUT(Pair, buf, 3), tda_test_pair_a_is_negative, nullptr, &val);

    TEST_ASSERT_EQUAL_INT64(7, buf[1].a);
    TEST_ASSERT_EQUAL_INT64(70, buf[1].b);
    TEST_ASSERT_EQUAL_INT64(1, buf[0].a); // untouched
    TEST_ASSERT_EQUAL_INT64(30, buf[2].b);
}

static void test_replace_of_an_empty_span_is_a_noop() {
    constexpr int32_t key = 1;
    constexpr int32_t val = 0;

    tda_span_replace(TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0), &key, &val, tda_eq_i32, nullptr);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_unique_collapses_runs);
    RUN_TEST(test_unique_keeps_non_adjacent_repeats);
    RUN_TEST(test_unique_of_all_equal_keeps_one);
    RUN_TEST(test_unique_without_runs_changes_nothing);
    RUN_TEST(test_unique_of_short_spans);
    RUN_TEST(test_unique_honours_the_callback);
    RUN_TEST(test_unique_compares_against_the_last_kept_elem);
    RUN_TEST(test_sort_then_unique_yields_a_set);
    RUN_TEST(test_unique_moves_whole_elems);

    RUN_TEST(test_remove_drops_every_occurrence);
    RUN_TEST(test_remove_of_a_missing_key_changes_nothing);
    RUN_TEST(test_remove_can_empty_the_span);
    RUN_TEST(test_remove_of_an_empty_span_is_zero);
    RUN_TEST(test_remove_if_drops_matching_elems);
    RUN_TEST(test_remove_if_passes_the_ctx_through);
    RUN_TEST(test_remove_if_keeps_the_order_of_survivors);
    RUN_TEST(test_remove_if_moves_whole_elems);

    RUN_TEST(test_replace_overwrites_every_occurrence);
    RUN_TEST(test_replace_of_a_missing_key_changes_nothing);
    RUN_TEST(test_replace_if_overwrites_matching_elems);
    RUN_TEST(test_replace_if_passes_the_ctx_through);
    RUN_TEST(test_replace_writes_whole_elems);
    RUN_TEST(test_replace_of_an_empty_span_is_a_noop);

    return UNITY_END();
}
