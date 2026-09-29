#include "tda/algo/copy.h"
#include "tda/core/util.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

static bool is_even(const void *elem, void *ctx) {
    TDA_UNUSED(ctx);

    return *(const int32_t *) elem % 2 == 0;
}

static bool greater_than(const void *elem, void *ctx) {
    return *(const int32_t *) elem > *(const int32_t *) ctx;
}

/* ========== copy ========== */

static void test_copy_transfers_every_elem() {
    constexpr int32_t src_buf[4] = {1, 2, 3, 4};
    int32_t dst_buf[4] = {0, 0, 0, 0};

    tda_span_copy(TDA_SPAN_FROM_DATA_MUT(int32_t, dst_buf, 4), TDA_SPAN_FROM_DATA(int32_t, src_buf, 4));

    TEST_ASSERT_EQUAL_INT32_ARRAY(src_buf, dst_buf, 4);
}

static void test_copy_empty_is_noop() {
    constexpr int32_t src_buf[2] = {7, 8};
    int32_t dst_buf[2] = {1, 2};

    tda_span_copy(TDA_SPAN_FROM_DATA_MUT(int32_t, dst_buf, 0), TDA_SPAN_FROM_DATA(int32_t, src_buf, 0));

    constexpr int32_t expected[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, dst_buf, 2);
}

// same buffer on both sides is a defined no-op, not an aliasing violation
static void test_copy_onto_itself_is_noop() {
    int32_t buf[3] = {1, 2, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3);

    tda_span_copy(s, tda_span_mut_to_span(s));

    constexpr int32_t expected[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 3);
}

static void test_copy_stays_within_the_subspan() {
    constexpr int32_t src_buf[2] = {7, 8};
    int32_t dst_buf[4] = {0, 0, 0, 0};
    const tda_SpanMut dst = TDA_SPAN_FROM_DATA_MUT(int32_t, dst_buf, 4);

    tda_span_copy(tda_span_sub_mut(dst, 1, 2), TDA_SPAN_FROM_DATA(int32_t, src_buf, 2));

    constexpr int32_t expected[4] = {0, 7, 8, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, dst_buf, 4);
}

// elem_size drives the copy, so a type wider than a word must arrive whole
static void test_copy_moves_whole_elements() {
    constexpr Pair src_buf[2] = {{1, 2}, {3, 4}};
    Pair dst_buf[2] = {{0, 0}, {0, 0}};

    tda_span_copy(TDA_SPAN_FROM_DATA_MUT(Pair, dst_buf, 2), TDA_SPAN_FROM_DATA(Pair, src_buf, 2));

    TEST_ASSERT_EQUAL_INT64(1, dst_buf[0].a);
    TEST_ASSERT_EQUAL_INT64(2, dst_buf[0].b);
    TEST_ASSERT_EQUAL_INT64(3, dst_buf[1].a);
    TEST_ASSERT_EQUAL_INT64(4, dst_buf[1].b);
}

/* ========== copy_overlapping ========== */

// shifting right overlaps: a plain memcpy would smear the first element
static void test_copy_overlapping_shifts_right() {
    int32_t buf[5] = {1, 2, 3, 4, 5};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    // buf[2..4] <- buf[1..3]
    tda_span_copy_overlapping(tda_span_sub_mut(s, 2, 3), tda_span_mut_to_span(tda_span_sub_mut(s, 1, 3)));

    constexpr int32_t expected[5] = {1, 2, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 5);
}

static void test_copy_overlapping_shifts_left() {
    int32_t buf[5] = {1, 2, 3, 4, 5};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    // buf[0..2] <- buf[2..4]
    tda_span_copy_overlapping(tda_span_sub_mut(s, 0, 3), tda_span_mut_to_span(tda_span_sub_mut(s, 2, 3)));

    constexpr int32_t expected[5] = {3, 4, 5, 4, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 5);
}

static void test_copy_overlapping_onto_itself_is_noop() {
    int32_t buf[3] = {1, 2, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3);

    tda_span_copy_overlapping(s, tda_span_mut_to_span(s));

    constexpr int32_t expected[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 3);
}

// disjoint ranges must behave exactly like tda_span_copy
static void test_copy_overlapping_handles_disjoint_ranges() {
    constexpr int32_t src_buf[2] = {7, 8};
    int32_t dst_buf[2] = {0, 0};

    tda_span_copy_overlapping(TDA_SPAN_FROM_DATA_MUT(int32_t, dst_buf, 2), TDA_SPAN_FROM_DATA(int32_t, src_buf, 2));

    TEST_ASSERT_EQUAL_INT32_ARRAY(src_buf, dst_buf, 2);
}

static void test_copy_overlapping_empty_is_noop() {
    int32_t buf[2] = {1, 2};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 0);

    tda_span_copy_overlapping(s, tda_span_mut_to_span(s));

    constexpr int32_t expected[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 2);
}

/* ========== copy_if ========== */

static void test_copy_if_takes_only_the_matching_elems() {
    constexpr int32_t src[6] = {1, 2, 3, 4, 5, 6};
    int32_t dst[6] = {0};

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 6),
        TDA_SPAN_FROM_DATA(int32_t, src, 6),
        is_even,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[3] = {2, 4, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 3);
}

// only the elems that pass need room, so a dst shorter than the source is fine
static void test_copy_if_needs_room_only_for_the_matches() {
    constexpr int32_t src[6] = {1, 2, 3, 4, 5, 6};
    int32_t dst[3] = {0};

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 3),
        TDA_SPAN_FROM_DATA(int32_t, src, 6),
        is_even,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[3] = {2, 4, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 3);
}

// what lies past the returned length is the caller's business, so the
// tail of dst must be left exactly as it was
static void test_copy_if_leaves_the_tail_of_dst_alone() {
    constexpr int32_t src[4] = {1, 2, 3, 4};
    int32_t dst[6] = {7, 7, 7, 7, 7, 7};

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 6),
        TDA_SPAN_FROM_DATA(int32_t, src, 4),
        is_even,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(2, n);
    constexpr int32_t want[6] = {2, 4, 7, 7, 7, 7};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 6);
}

static void test_copy_if_takes_everything_when_all_match() {
    constexpr int32_t src[3] = {2, 4, 6};
    int32_t dst[3] = {0};

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 3),
        TDA_SPAN_FROM_DATA(int32_t, src, 3),
        is_even,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, dst, 3);
}

static void test_copy_if_takes_nothing_when_none_match() {
    constexpr int32_t src[3] = {1, 3, 5};
    int32_t dst[3] = {9, 9, 9};

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 3),
        TDA_SPAN_FROM_DATA(int32_t, src, 3),
        is_even,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(0, n);
    constexpr int32_t want[3] = {9, 9, 9};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 3);
}

static void test_copy_if_of_an_empty_source_is_zero() {
    int32_t dst[2] = {9, 9};

    TEST_ASSERT_EQUAL_size_t(
        0,
        tda_span_copy_if(
            TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 2),
            TDA_SPAN_FROM_DATA(int32_t, nullptr, 0),
            is_even,
            nullptr
        )
    );
}

static void test_copy_if_passes_the_ctx_through() {
    constexpr int32_t src[5] = {1, 5, 2, 4, 3};
    int32_t dst[5] = {0};
    int32_t bound = 3;

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 5),
        TDA_SPAN_FROM_DATA(int32_t, src, 5),
        greater_than,
        &bound
    );

    TEST_ASSERT_EQUAL_size_t(2, n);
    constexpr int32_t want[2] = {5, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 2);
}

static void test_copy_if_keeps_the_source_order() {
    constexpr int32_t src[7] = {6, 1, 4, 3, 2, 5, 8};
    int32_t dst[7] = {0};

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 7),
        TDA_SPAN_FROM_DATA(int32_t, src, 7),
        is_even,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(4, n);
    constexpr int32_t want[4] = {6, 4, 2, 8};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 4);
}

static void test_copy_if_moves_whole_elems() {
    constexpr Pair src[3] = {{-1, 10}, {1, 20}, {-2, 30}};
    Pair dst[3] = {0};

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(Pair, dst, 3),
        TDA_SPAN_FROM_DATA(Pair, src, 3),
        tda_test_pair_a_is_positive,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(1, n);
    TEST_ASSERT_EQUAL_INT64(1, dst[0].a);
    TEST_ASSERT_EQUAL_INT64(20, dst[0].b);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_copy_transfers_every_elem);
    RUN_TEST(test_copy_empty_is_noop);
    RUN_TEST(test_copy_onto_itself_is_noop);
    RUN_TEST(test_copy_stays_within_the_subspan);
    RUN_TEST(test_copy_moves_whole_elements);

    RUN_TEST(test_copy_overlapping_shifts_right);
    RUN_TEST(test_copy_overlapping_shifts_left);
    RUN_TEST(test_copy_overlapping_onto_itself_is_noop);
    RUN_TEST(test_copy_overlapping_handles_disjoint_ranges);
    RUN_TEST(test_copy_overlapping_empty_is_noop);

    RUN_TEST(test_copy_if_takes_only_the_matching_elems);
    RUN_TEST(test_copy_if_needs_room_only_for_the_matches);
    RUN_TEST(test_copy_if_leaves_the_tail_of_dst_alone);
    RUN_TEST(test_copy_if_takes_everything_when_all_match);
    RUN_TEST(test_copy_if_takes_nothing_when_none_match);
    RUN_TEST(test_copy_if_of_an_empty_source_is_zero);
    RUN_TEST(test_copy_if_passes_the_ctx_through);
    RUN_TEST(test_copy_if_keeps_the_source_order);
    RUN_TEST(test_copy_if_moves_whole_elems);

    return UNITY_END();
}
