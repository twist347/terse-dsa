// The typed macros over algo: each takes its key or value as &(K[1]){ (key) }[0], so a
// scalar, a struct variable and a struct literal in parentheses all go in, and a key of
// another type than the elem's goes in as well. Each case checks the macro against the
// function it wraps.

#include "tda/algo/fill.h"
#include "tda/algo/modify.h"
#include "tda/algo/search.h"
#include "tda/core/cmp.h"
#include "tda/core/span.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// a key of its own type: the elem's 'a' field alone, compared on the right
static bool eq_pair_by_a(const void *elem, const void *key, void *) {
    return ((const Pair *) elem)->a == *(const int64_t *) key;
}

static int cmp_pair_by_a(const void *elem, const void *key, void *) {
    return tda_cmp_i64(&((const Pair *) elem)->a, key, nullptr);
}

static bool eq_pair(const void *lhs, const void *rhs, void *) {
    const Pair *l = lhs, *r = rhs;

    return l->a == r->a && l->b == r->b;
}

static bool is_odd(const void *elem, void *) {
    return *(const int32_t *) elem % 2 != 0;
}

/* ========== search ========== */

static void test_find_macros_take_a_scalar_key() {
    const tda_Span s = TDA_SPAN_OF(int32_t, 4, 7, 7, 2, 7);

    size_t idx = 99;
    TEST_ASSERT_TRUE(TDA_SPAN_FIND(int32_t, s, 7, tda_eq_i32, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
    TEST_ASSERT_TRUE(TDA_SPAN_FIND_LAST(int32_t, s, 7, tda_eq_i32, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(4, idx);
    TEST_ASSERT_TRUE(TDA_SPAN_FIND_RUN(int32_t, s, 7, 2, tda_eq_i32, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);

    idx = 99;
    TEST_ASSERT_FALSE(TDA_SPAN_FIND(int32_t, s, 5, tda_eq_i32, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(99, idx);

    TEST_ASSERT_TRUE(TDA_SPAN_CONTAINS(int32_t, s, 2, tda_eq_i32, nullptr));
    TEST_ASSERT_FALSE(TDA_SPAN_CONTAINS(int32_t, s, 5, tda_eq_i32, nullptr));
    TEST_ASSERT_EQUAL_size_t(3, TDA_SPAN_COUNT(int32_t, s, 7, tda_eq_i32, nullptr));
}

static void test_find_macros_take_a_struct_key() {
    const tda_Span s = TDA_SPAN_OF(Pair, {1, 2}, {3, 4}, {3, 4});
    const Pair p = {3, 4};

    size_t idx = 99;
    TEST_ASSERT_TRUE(TDA_SPAN_FIND(Pair, s, p, eq_pair, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
    TEST_ASSERT_TRUE(TDA_SPAN_FIND_LAST(Pair, s, ((Pair){3, 4}), eq_pair, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(2, idx);
    TEST_ASSERT_TRUE(TDA_SPAN_CONTAINS(Pair, s, ((Pair){1, 2}), eq_pair, nullptr));
    TEST_ASSERT_EQUAL_size_t(2, TDA_SPAN_COUNT(Pair, s, p, eq_pair, nullptr));
}

// the key goes to the right, so a span of pairs is searched by a bare int64_t
static void test_find_macros_take_a_key_of_another_type() {
    const tda_Span s = TDA_SPAN_OF(Pair, {1, 10}, {2, 20}, {2, 30});

    size_t idx = 99;
    TEST_ASSERT_TRUE(TDA_SPAN_FIND(int64_t, s, 2, eq_pair_by_a, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
    TEST_ASSERT_EQUAL_size_t(2, TDA_SPAN_COUNT(int64_t, s, 2, eq_pair_by_a, nullptr));
    TEST_ASSERT_FALSE(TDA_SPAN_CONTAINS(int64_t, s, 3, eq_pair_by_a, nullptr));
}

static void test_bound_macros_match_the_functions() {
    const tda_Span s = TDA_SPAN_OF(int32_t, 1, 3, 3, 3, 8);

    TEST_ASSERT_EQUAL_size_t(1, TDA_SPAN_LOWER_BOUND(int32_t, s, 3, tda_cmp_i32, nullptr));
    TEST_ASSERT_EQUAL_size_t(4, TDA_SPAN_UPPER_BOUND(int32_t, s, 3, tda_cmp_i32, nullptr));
    TEST_ASSERT_EQUAL_size_t(4, TDA_SPAN_LOWER_BOUND(int32_t, s, 5, tda_cmp_i32, nullptr));

    const tda_Range r = TDA_SPAN_EQUAL_RANGE(int32_t, s, 3, tda_cmp_i32, nullptr);
    TEST_ASSERT_EQUAL_size_t(1, r.lo);
    TEST_ASSERT_EQUAL_size_t(4, r.hi);

    size_t idx = 99;
    TEST_ASSERT_TRUE(TDA_SPAN_BINARY_SEARCH(int32_t, s, 8, tda_cmp_i32, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(4, idx);
    TEST_ASSERT_FALSE(TDA_SPAN_BINARY_SEARCH(int32_t, s, 2, tda_cmp_i32, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(4, idx);
}

// a span of records sorted by id, searched with the id
static void test_bound_macros_take_a_key_of_another_type() {
    const tda_Span s = TDA_SPAN_OF(Pair, {1, 0}, {4, 0}, {4, 1}, {9, 0});

    TEST_ASSERT_EQUAL_size_t(1, TDA_SPAN_LOWER_BOUND(int64_t, s, 4, cmp_pair_by_a, nullptr));
    TEST_ASSERT_EQUAL_size_t(3, TDA_SPAN_UPPER_BOUND(int64_t, s, 4, cmp_pair_by_a, nullptr));

    size_t idx = 99;
    TEST_ASSERT_TRUE(TDA_SPAN_BINARY_SEARCH(int64_t, s, 9, cmp_pair_by_a, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(3, idx);
}

/* ========== fill ========== */

static void test_fill_takes_a_scalar_and_a_struct() {
    int32_t nums[3] = {};
    TDA_SPAN_FILL(int32_t, TDA_SPAN_FROM_DATA_MUT(int32_t, nums, 3), 7);
    TEST_ASSERT_EQUAL_INT32_ARRAY(((int32_t[]){7, 7, 7}), nums, 3);

    Pair pairs[2] = {};
    TDA_SPAN_FILL(Pair, TDA_SPAN_FROM_DATA_MUT(Pair, pairs, 2), ((Pair){5, 6}));
    for (size_t i = 0; i < 2; ++i) {
        TEST_ASSERT_EQUAL_INT64(5, pairs[i].a);
        TEST_ASSERT_EQUAL_INT64(6, pairs[i].b);
    }
}

/* ========== modify ========== */

static void test_remove_takes_a_scalar_and_another_type() {
    int32_t nums[] = {1, 2, 1, 3};
    const size_t len = TDA_SPAN_REMOVE(
        int32_t,
        TDA_SPAN_FROM_DATA_MUT(int32_t, nums, 4),
        1,
        tda_eq_i32,
        nullptr
    );
    TEST_ASSERT_EQUAL_size_t(2, len);
    TEST_ASSERT_EQUAL_INT32_ARRAY(((int32_t[]){2, 3}), nums, 2);

    Pair pairs[] = {{1, 10}, {2, 20}, {1, 30}};
    const size_t kept = TDA_SPAN_REMOVE(
        int64_t,
        TDA_SPAN_FROM_DATA_MUT(Pair, pairs, 3),
        1,
        eq_pair_by_a,
        nullptr
    );
    TEST_ASSERT_EQUAL_size_t(1, kept);
    TEST_ASSERT_EQUAL_INT64(20, pairs[0].b);
}

// a key of one type and a value of the elem's: a pair whose 'a' is 2 becomes {9, 9}
static void test_replace_takes_a_key_and_a_value_of_different_types() {
    Pair pairs[] = {{1, 10}, {2, 20}, {2, 30}};

    TDA_SPAN_REPLACE(
        int64_t,
        Pair,
        TDA_SPAN_FROM_DATA_MUT(Pair, pairs, 3),
        2,
        ((Pair){9, 9}),
        eq_pair_by_a,
        nullptr
    );

    TEST_ASSERT_EQUAL_INT64(1, pairs[0].a);
    for (size_t i = 1; i < 3; ++i) {
        TEST_ASSERT_EQUAL_INT64(9, pairs[i].a);
        TEST_ASSERT_EQUAL_INT64(9, pairs[i].b);
    }

    int32_t nums[] = {1, 2, 1};
    TDA_SPAN_REPLACE(
        int32_t,
        int32_t,
        TDA_SPAN_FROM_DATA_MUT(int32_t, nums, 3),
        1,
        0,
        tda_eq_i32,
        nullptr
    );
    TEST_ASSERT_EQUAL_INT32_ARRAY(((int32_t[]){0, 2, 0}), nums, 3);
}

static void test_replace_if_takes_a_value() {
    int32_t nums[] = {1, 2, 3, 4};

    TDA_SPAN_REPLACE_IF(int32_t, TDA_SPAN_FROM_DATA_MUT(int32_t, nums, 4), -1, is_odd, nullptr);

    TEST_ASSERT_EQUAL_INT32_ARRAY(((int32_t[]){-1, 2, -1, 4}), nums, 4);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_find_macros_take_a_scalar_key);
    RUN_TEST(test_find_macros_take_a_struct_key);
    RUN_TEST(test_find_macros_take_a_key_of_another_type);
    RUN_TEST(test_bound_macros_match_the_functions);
    RUN_TEST(test_bound_macros_take_a_key_of_another_type);
    RUN_TEST(test_fill_takes_a_scalar_and_a_struct);
    RUN_TEST(test_remove_takes_a_scalar_and_another_type);
    RUN_TEST(test_replace_takes_a_key_and_a_value_of_different_types);
    RUN_TEST(test_replace_if_takes_a_value);

    return UNITY_END();
}
