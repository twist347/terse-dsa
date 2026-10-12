#include "tda/algo/transform.h"
#include "tda/core/util.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

static void double_i32(void *dst, const void *src, void *ctx) {
    TDA_UNUSED(ctx);

    *(int32_t *) dst = *(const int32_t *) src * 2;
}

// ctx as the operation's parameter
static void scale_i32(void *dst, const void *src, void *ctx) {
    *(int32_t *) dst = *(const int32_t *) src * *(const int32_t *) ctx;
}

static void pair_sum_into_i64(void *dst, const void *src, void *ctx) {
    TDA_UNUSED(ctx);

    const Pair *p = src;
    *(int64_t *) dst = p->a + p->b;
}

static void swap_pair_fields(void *dst, const void *src, void *ctx) {
    TDA_UNUSED(ctx);

    const Pair in = *(const Pair *) src;
    ((Pair *) dst)->a = in.b;
    ((Pair *) dst)->b = in.a;
}

static void add_i32(void *dst, const void *a, const void *b, void *ctx) {
    TDA_UNUSED(ctx);

    *(int32_t *) dst = *(const int32_t *) a + *(const int32_t *) b;
}

// deliberately not symmetric: min would pass even with the operands swapped
static void sub_i32(void *dst, const void *a, const void *b, void *ctx) {
    TDA_UNUSED(ctx);

    *(int32_t *) dst = *(const int32_t *) a - *(const int32_t *) b;
}

/* ========== transform ========== */

static void test_transform_maps_every_elem() {
    constexpr int32_t src[4] = {1, 2, 3, 4};
    int32_t dst[4] = {0};

    tda_span_transform(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 4),
        TDA_SPAN_FROM_DATA(int32_t, src, 4),
        double_i32,
        nullptr
    );

    constexpr int32_t want[4] = {2, 4, 6, 8};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 4);
}

// mapping a span onto itself is the common case and is allowed
static void test_transform_works_in_place() {
    int32_t buf[4] = {1, 2, 3, 4};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);

    tda_span_transform(s, tda_span_mut_to_span(s), double_i32, nullptr);

    constexpr int32_t want[4] = {2, 4, 6, 8};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 4);
}

static void test_transform_leaves_the_source_alone() {
    constexpr int32_t src[3] = {1, 2, 3};
    int32_t dst[3] = {0};

    tda_span_transform(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 3),
        TDA_SPAN_FROM_DATA(int32_t, src, 3),
        double_i32,
        nullptr
    );

    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, src, 3);
}

static void test_transform_of_an_empty_span_writes_nothing() {
    tda_span_transform(
        TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0),
        TDA_SPAN_FROM_DATA(int32_t, nullptr, 0),
        double_i32,
        nullptr
    );
}

static void test_transform_passes_the_ctx_through() {
    constexpr int32_t src[3] = {1, 2, 3};
    int32_t dst[3] = {0};
    int32_t factor = 10;

    tda_span_transform(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 3),
        TDA_SPAN_FROM_DATA(int32_t, src, 3),
        scale_i32,
        &factor
    );

    constexpr int32_t want[3] = {10, 20, 30};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 3);
}

// the elem type may change on the way, as long as the widths match
static void test_transform_may_narrow_the_elem() {
    constexpr Pair src[3] = {{1, 10}, {2, 20}, {3, 30}};
    int64_t dst[3] = {0};

    tda_span_transform(
        TDA_SPAN_FROM_DATA_MUT(int64_t, dst, 3),
        TDA_SPAN_FROM_DATA(Pair, src, 3),
        pair_sum_into_i64,
        nullptr
    );

    TEST_ASSERT_EQUAL_INT64(11, dst[0]);
    TEST_ASSERT_EQUAL_INT64(33, dst[2]);
}

static void test_transform_writes_whole_elems() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};
    Pair dst[2] = {0};

    tda_span_transform(
        TDA_SPAN_FROM_DATA_MUT(Pair, dst, 2),
        TDA_SPAN_FROM_DATA(Pair, src, 2),
        swap_pair_fields,
        nullptr
    );

    TEST_ASSERT_EQUAL_INT64(2, dst[0].a);
    TEST_ASSERT_EQUAL_INT64(1, dst[0].b);
    TEST_ASSERT_EQUAL_INT64(4, dst[1].a);
    TEST_ASSERT_EQUAL_INT64(3, dst[1].b);
}

/* ========== zip_with ========== */

static void test_zip_with_walks_both_sources_in_step() {
    constexpr int32_t a[4] = {1, 2, 3, 4};
    constexpr int32_t b[4] = {10, 20, 30, 40};
    int32_t dst[4] = {0};

    tda_span_zip_with(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 4),
        TDA_SPAN_FROM_DATA(int32_t, a, 4),
        TDA_SPAN_FROM_DATA(int32_t, b, 4),
        add_i32,
        nullptr
    );

    constexpr int32_t want[4] = {11, 22, 33, 44};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 4);
}

// the operands keep their order: a first, b second
static void test_zip_with_keeps_the_operand_order() {
    constexpr int32_t a[3] = {5, 1, 7};
    constexpr int32_t b[3] = {2, 9, 3};
    int32_t dst[3] = {0};

    tda_span_zip_with(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 3),
        TDA_SPAN_FROM_DATA(int32_t, a, 3),
        TDA_SPAN_FROM_DATA(int32_t, b, 3),
        sub_i32,
        nullptr
    );

    constexpr int32_t want[3] = {3, -8, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 3);
}

static void test_zip_with_can_write_into_one_of_its_sources() {
    int32_t a[3] = {1, 2, 3};
    constexpr int32_t b[3] = {10, 20, 30};
    const tda_SpanMut dst = TDA_SPAN_FROM_DATA_MUT(int32_t, a, 3);

    tda_span_zip_with(dst, tda_span_mut_to_span(dst), TDA_SPAN_FROM_DATA(int32_t, b, 3), add_i32, nullptr);

    constexpr int32_t want[3] = {11, 22, 33};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, a, 3);
}

static void test_zip_with_of_empty_spans_writes_nothing() {
    tda_span_zip_with(
        TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0),
        TDA_SPAN_FROM_DATA(int32_t, nullptr, 0),
        TDA_SPAN_FROM_DATA(int32_t, nullptr, 0),
        add_i32,
        nullptr
    );
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_transform_maps_every_elem);
    RUN_TEST(test_transform_works_in_place);
    RUN_TEST(test_transform_leaves_the_source_alone);
    RUN_TEST(test_transform_of_an_empty_span_writes_nothing);
    RUN_TEST(test_transform_passes_the_ctx_through);
    RUN_TEST(test_transform_may_narrow_the_elem);
    RUN_TEST(test_transform_writes_whole_elems);

    RUN_TEST(test_zip_with_walks_both_sources_in_step);
    RUN_TEST(test_zip_with_keeps_the_operand_order);
    RUN_TEST(test_zip_with_can_write_into_one_of_its_sources);
    RUN_TEST(test_zip_with_of_empty_spans_writes_nothing);

    return UNITY_END();
}
