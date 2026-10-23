#include "tda/algo/fill.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// iota: the index alone is enough
static void gen_index(void *dst, size_t idx, void *) {
    *(int32_t *) dst = (int32_t) idx;
}

// a counter in ctx, for a sequence the index cannot express
static void gen_doubling(void *dst, size_t, void *ctx) {
    int32_t *next = ctx;
    *(int32_t *) dst = *next;
    *next *= 2;
}

static void gen_pair(void *dst, size_t idx, void *) {
    ((Pair *) dst)->a = (int64_t) idx;
    ((Pair *) dst)->b = (int64_t) idx * 10;
}

/* ========== fill ========== */

static void test_fill_writes_every_elem() {
    int32_t buf[4] = {0, 0, 0, 0};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);

    constexpr int32_t val = 7;
    tda_span_fill(s, &val);

    constexpr int32_t expected[4] = {7, 7, 7, 7};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 4);
}

static void test_fill_empty_is_noop() {
    int32_t buf[2] = {1, 2};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 0);

    constexpr int32_t val = 9;
    tda_span_fill(s, &val);

    constexpr int32_t expected[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 2);
}

// filling a subspan must not touch its neighbours
static void test_fill_stays_within_the_subspan() {
    int32_t buf[5] = {0, 0, 0, 0, 0};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    constexpr int32_t val = 8;
    tda_span_fill(tda_span_sub_mut(s, 1, 3), &val);

    constexpr int32_t expected[5] = {0, 8, 8, 8, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 5);
}

// elem_size drives the write, so a type wider than a word must be copied whole
static void test_fill_copies_whole_elements() {
    Pair buf[2] = {{0, 0}, {0, 0}};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(Pair, buf, 2);

    constexpr Pair val = {11, 22};
    tda_span_fill(s, &val);

    TEST_ASSERT_EQUAL_INT64(11, buf[0].a);
    TEST_ASSERT_EQUAL_INT64(22, buf[0].b);
    TEST_ASSERT_EQUAL_INT64(11, buf[1].a);
    TEST_ASSERT_EQUAL_INT64(22, buf[1].b);
}

// the source is read once per element, so it may live inside the span itself
static void test_fill_from_an_element_of_the_same_span() {
    int32_t buf[3] = {5, 1, 2};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3);

    tda_span_fill(s, &buf[0]);

    constexpr int32_t expected[3] = {5, 5, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 3);
}

/* ========== fill_zero ========== */

static void test_fill_zero_clears_every_byte() {
    int32_t buf[3] = {1, 2, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3);

    tda_span_fill_zero(s);

    constexpr int32_t expected[3] = {0, 0, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 3);
}

static void test_fill_zero_empty_is_noop() {
    int32_t buf[2] = {1, 2};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 0);

    tda_span_fill_zero(s);

    constexpr int32_t expected[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 2);
}

static void test_fill_zero_stays_within_the_subspan() {
    int32_t buf[4] = {1, 2, 3, 4};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);

    tda_span_fill_zero(tda_span_sub_mut(s, 1, 2));

    constexpr int32_t expected[4] = {1, 0, 0, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 4);
}

// an empty view over null must not reach memset with a null pointer
static void test_fill_zero_null_view_is_noop() {
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0);

    tda_span_fill_zero(s);
}

/* ========== generate ========== */

// generate + the index is iota
static void test_generate_fills_from_the_index() {
    int32_t buf[5] = {9, 9, 9, 9, 9};

    tda_span_generate(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), gen_index, nullptr);

    constexpr int32_t want[5] = {0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

static void test_generate_passes_the_ctx_through() {
    int32_t buf[4] = {0};
    int32_t next = 3;

    tda_span_generate(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), gen_doubling, &next);

    constexpr int32_t want[4] = {3, 6, 12, 24};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 4);
    TEST_ASSERT_EQUAL_INT32(48, next); // the ctx carried the state along
}

static void test_generate_of_an_empty_span_is_a_noop() {
    tda_span_generate(TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0), gen_index, nullptr);
}

static void test_generate_writes_whole_elems() {
    Pair buf[3] = {0};

    tda_span_generate(TDA_SPAN_FROM_DATA_MUT(Pair, buf, 3), gen_pair, nullptr);

    TEST_ASSERT_EQUAL_INT64(2, buf[2].a);
    TEST_ASSERT_EQUAL_INT64(20, buf[2].b);
    TEST_ASSERT_EQUAL_INT64(0, buf[0].a);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_fill_writes_every_elem);
    RUN_TEST(test_fill_empty_is_noop);
    RUN_TEST(test_fill_stays_within_the_subspan);
    RUN_TEST(test_fill_copies_whole_elements);
    RUN_TEST(test_fill_from_an_element_of_the_same_span);

    RUN_TEST(test_fill_zero_clears_every_byte);
    RUN_TEST(test_fill_zero_empty_is_noop);
    RUN_TEST(test_fill_zero_stays_within_the_subspan);
    RUN_TEST(test_fill_zero_null_view_is_noop);

    RUN_TEST(test_generate_fills_from_the_index);
    RUN_TEST(test_generate_passes_the_ctx_through);
    RUN_TEST(test_generate_of_an_empty_span_is_a_noop);
    RUN_TEST(test_generate_writes_whole_elems);

    return UNITY_END();
}
