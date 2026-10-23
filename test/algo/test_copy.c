#include "tda/algo/copy.h"
#include "tda/core/util.h"

#include "support/i32.h"
#include "support/pair.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
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
        tda_test_i32_is_even,
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
        tda_test_i32_is_even,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[3] = {2, 4, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 3);
}

// the tail of dst past the returned length belongs to the caller and must
// be left as it was
static void test_copy_if_leaves_the_tail_of_dst_alone() {
    constexpr int32_t src[4] = {1, 2, 3, 4};
    int32_t dst[6] = {7, 7, 7, 7, 7, 7};

    const size_t n = tda_span_copy_if(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 6),
        TDA_SPAN_FROM_DATA(int32_t, src, 4),
        tda_test_i32_is_even,
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
        tda_test_i32_is_even,
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
        tda_test_i32_is_even,
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
            tda_test_i32_is_even,
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
        tda_test_i32_greater_than,
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
        tda_test_i32_is_even,
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

/* ========== sample ========== */

// the picks come out in source order, so over an increasing source they increase
static void test_sample_picks_in_source_order() {
    constexpr int32_t src[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int32_t dst[3] = {0};

    tda_Rng rng = tda_rng_from_seed(1);
    const size_t n = tda_span_sample(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 3),
        TDA_SPAN_FROM_DATA(int32_t, src, 8),
        &rng
    );

    TEST_ASSERT_EQUAL_size_t(3, n);
    TEST_ASSERT_TRUE(dst[0] < dst[1]);
    TEST_ASSERT_TRUE(dst[1] < dst[2]);
    TEST_ASSERT_TRUE(dst[0] >= 0);
    TEST_ASSERT_TRUE(dst[2] <= 7);
}

// asking for at least the source length copies it all and leaves the tail of dst alone
static void test_sample_of_more_than_there_is_copies_the_whole_source() {
    constexpr int32_t src[3] = {4, 5, 6};
    int32_t dst[5] = {-1, -1, -1, -1, -1};

    tda_Rng rng = tda_rng_from_seed(2);
    const size_t n = tda_span_sample(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 5),
        TDA_SPAN_FROM_DATA(int32_t, src, 3),
        &rng
    );

    TEST_ASSERT_EQUAL_size_t(3, n);
    constexpr int32_t want[5] = {4, 5, 6, -1, -1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 5);
}

// nothing wanted or nothing to pick from: no write and no draw, so the rng
// state is unchanged
static void test_sample_of_nothing_draws_nothing() {
    constexpr int32_t src[3] = {1, 2, 3};
    int32_t dst[2] = {-1, -1};

    tda_Rng rng = tda_rng_from_seed(3);
    tda_Rng untouched = tda_rng_from_seed(3);

    TEST_ASSERT_EQUAL_size_t(
        0,
        tda_span_sample(TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 0), TDA_SPAN_FROM_DATA(int32_t, src, 3), &rng)
    );
    TEST_ASSERT_EQUAL_size_t(
        0,
        tda_span_sample(TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 2), TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), &rng)
    );

    TEST_ASSERT_EQUAL_INT32(-1, dst[0]);
    TEST_ASSERT_EQUAL_UINT64(tda_rng_u64(&untouched), tda_rng_u64(&rng));
}

// the sampling property: each of the ten in-order pairs out of five comes up equally
// often, and nothing else ever does
static void test_sample_reaches_every_pick_equally_often() {
    static constexpr size_t TRIALS = 100000;
    constexpr int32_t src[5] = {0, 1, 2, 3, 4};

    tda_Rng rng = tda_rng_from_seed(4);
    size_t seen[5][5] = {0};

    for (size_t i = 0; i < TRIALS; ++i) {
        int32_t dst[2];
        const size_t n = tda_span_sample(
            TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 2),
            TDA_SPAN_FROM_DATA(int32_t, src, 5),
            &rng
        );
        TEST_ASSERT_EQUAL_size_t(2, n);

        ++seen[dst[0]][dst[1]];
    }

    constexpr double expected = (double) TRIALS / 10.0;
    for (size_t i = 0; i < 5; ++i) {
        for (size_t j = 0; j < 5; ++j) {
            if (i < j) {
                TEST_ASSERT_DOUBLE_WITHIN(expected * 0.04, expected, (double) seen[i][j]);
            } else {
                TEST_ASSERT_EQUAL_size_t(0, seen[i][j]);
            }
        }
    }
}

static void test_sample_replays_the_same_seed() {
    constexpr int32_t src[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int32_t a[4] = {0};
    int32_t b[4] = {0};

    tda_Rng one = tda_rng_from_seed(5);
    tda_Rng two = tda_rng_from_seed(5);

    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, src, 10);
    TEST_ASSERT_EQUAL_size_t(4, tda_span_sample(TDA_SPAN_FROM_DATA_MUT(int32_t, a, 4), s, &one));
    TEST_ASSERT_EQUAL_size_t(4, tda_span_sample(TDA_SPAN_FROM_DATA_MUT(int32_t, b, 4), s, &two));

    TEST_ASSERT_EQUAL_INT32_ARRAY(a, b, 4);
}

static void test_sample_moves_whole_elems() {
    constexpr Pair src[5] = {{1, 100}, {2, 200}, {3, 300}, {4, 400}, {5, 500}};
    Pair dst[3] = {0};

    tda_Rng rng = tda_rng_from_seed(6);
    const size_t n = tda_span_sample(
        TDA_SPAN_FROM_DATA_MUT(Pair, dst, 3),
        TDA_SPAN_FROM_DATA(Pair, src, 5),
        &rng
    );

    TEST_ASSERT_EQUAL_size_t(3, n);
    for (size_t i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_INT64(dst[i].a * 100, dst[i].b);
    }
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

    RUN_TEST(test_sample_picks_in_source_order);
    RUN_TEST(test_sample_of_more_than_there_is_copies_the_whole_source);
    RUN_TEST(test_sample_of_nothing_draws_nothing);
    RUN_TEST(test_sample_reaches_every_pick_equally_often);
    RUN_TEST(test_sample_replays_the_same_seed);
    RUN_TEST(test_sample_moves_whole_elems);

    return UNITY_END();
}
