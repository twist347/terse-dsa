#include "tda/core/rng.h"

#include <unity.h>

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// enough draws that a bucket off by a percent shows, few enough that the file runs in
// well under a second
static constexpr size_t TRIALS = 200000;

/* ========== the sequence itself ========== */

// the known-answer test: the first draws of a seed, taken from the reference
// xoshiro256++ of Blackman and Vigna seeded through their SplitMix64. This is what says
// the implementation is the algorithm it claims and not merely something that looks
// random — a rotate off by one bit would still pass every distribution test below.
static void test_rng_matches_the_reference_stream() {
    static constexpr uint64_t seed_0[] = {
        UINT64_C(0x53175d61490b23df),
        UINT64_C(0x61da6f3dc380d507),
        UINT64_C(0x5c0fdf91ec9a7bfc),
        UINT64_C(0x02eebf8c3bbe5e1a),
        UINT64_C(0x7eca04ebaf4a5eea),
    };
    static constexpr uint64_t seed_42[] = {
        UINT64_C(0xd0764d4f4476689f),
        UINT64_C(0x519e4174576f3791),
        UINT64_C(0xfbe07cfb0c24ed8c),
        UINT64_C(0xb37d9f600cd835b8),
        UINT64_C(0xcb231c3874846a73),
    };

    tda_Rng rng = tda_rng_from_seed(0);
    for (size_t i = 0; i < sizeof(seed_0) / sizeof(*seed_0); ++i) {
        TEST_ASSERT_EQUAL_UINT64(seed_0[i], tda_rng_u64(&rng));
    }

    rng = tda_rng_from_seed(42);
    for (size_t i = 0; i < sizeof(seed_42) / sizeof(*seed_42); ++i) {
        TEST_ASSERT_EQUAL_UINT64(seed_42[i], tda_rng_u64(&rng));
    }
}

// what the whole module is for: a failure that only happens on the 4000th draw can be
// replayed
static void test_rng_replays_the_same_seed() {
    tda_Rng a = tda_rng_from_seed(12345);
    tda_Rng b = tda_rng_from_seed(12345);

    for (size_t i = 0; i < 1000; ++i) {
        TEST_ASSERT_EQUAL_UINT64(tda_rng_u64(&a), tda_rng_u64(&b));
    }
}

// seed 0 is the one a hand-rolled xorshift would sit on forever; SplitMix64 is what makes
// it as good a seed as any
static void test_rng_takes_zero_as_a_seed() {
    tda_Rng zero = tda_rng_from_seed(0);
    tda_Rng one = tda_rng_from_seed(1);

    size_t agreements = 0;
    for (size_t i = 0; i < 100; ++i) {
        const uint64_t val = tda_rng_u64(&zero);
        TEST_ASSERT_NOT_EQUAL_UINT64(0, val);

        if (val == tda_rng_u64(&one)) {
            ++agreements;
        }
    }

    TEST_ASSERT_EQUAL_size_t(0, agreements);
}

// the struct is transparent, so a copy is a fork of the stream — both halves replay
static void test_rng_copies_as_a_value() {
    tda_Rng rng = tda_rng_from_seed(7);
    for (size_t i = 0; i < 10; ++i) {
        (void) tda_rng_u64(&rng);
    }

    tda_Rng fork = rng;
    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_UINT64(tda_rng_u64(&rng), tda_rng_u64(&fork));
    }
}

// u32 is the top half of a whole draw, so it must both replay and advance the generator
// exactly as u64 does — and it must not be the low half, which is the weaker one here
static void test_rng_u32_is_the_top_half_of_a_draw() {
    tda_Rng a = tda_rng_from_seed(12345);
    tda_Rng b = tda_rng_from_seed(12345);

    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_UINT32((uint32_t) (tda_rng_u64(&a) >> 32), tda_rng_u32(&b));
    }
}

// a draw over the whole width has to reach both ends of it
static void test_rng_u32_takes_the_full_width() {
    tda_Rng rng = tda_rng_from_seed(1);

    uint32_t seen_bits = 0;
    for (size_t i = 0; i < 200; ++i) {
        seen_bits |= tda_rng_u32(&rng);
    }

    TEST_ASSERT_EQUAL_HEX32(UINT32_MAX, seen_bits);
}

/* ========== bounded ints ========== */

static void test_rng_u64_max_stays_in_range() {
    tda_Rng rng = tda_rng_from_seed(1);

    static constexpr uint64_t maxes[] = {0, 1, 2, 6, 7, 8, 999, UINT64_C(1) << 40};

    for (size_t m = 0; m < sizeof(maxes) / sizeof(*maxes); ++m) {
        for (size_t i = 0; i < 2000; ++i) {
            TEST_ASSERT_TRUE(tda_rng_u64_max(&rng, maxes[m]) <= maxes[m]);
        }
    }
}

// the whole width is the one bound with nothing to reject and no range to compute
static void test_rng_u64_max_takes_the_full_width() {
    tda_Rng bounded = tda_rng_from_seed(99);
    tda_Rng raw = tda_rng_from_seed(99);

    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_UINT64(tda_rng_u64(&raw), tda_rng_u64_max(&bounded, UINT64_MAX));
    }
}

static void test_rng_u32_max_stays_in_range() {
    tda_Rng rng = tda_rng_from_seed(2);

    static constexpr uint32_t maxes[] = {0, 1, 5, 255, 256, UINT32_MAX};

    for (size_t m = 0; m < sizeof(maxes) / sizeof(*maxes); ++m) {
        for (size_t i = 0; i < 2000; ++i) {
            TEST_ASSERT_TRUE(tda_rng_u32_max(&rng, maxes[m]) <= maxes[m]);
        }
    }
}

// three does not divide 2^64, so one of the three slots has a draw more than the others
// to give away — the slot Lemire's rejection is there to hold back. A product taken from
// the wrong half, or a threshold off by one, shows up here as a lopsided bucket.
//
// What this cannot see is the modulo bias itself: over a 64-bit draw and a bound this
// small it is one part in 2^62, far under what any bucket count could resolve. The
// rejection earns its keep at bounds near the top of the range, where the same skew is
// the difference between uniform and twice as likely.
static void test_rng_u64_max_is_flat_over_a_bound_that_does_not_divide_the_width() {
    tda_Rng rng = tda_rng_from_seed(4);

    size_t buckets[3] = {0};
    for (size_t i = 0; i < TRIALS; ++i) {
        ++buckets[tda_rng_u64_max(&rng, 2)];
    }

    const double expected = (double) TRIALS / 3.0;
    for (size_t b = 0; b < 3; ++b) {
        TEST_ASSERT_DOUBLE_WITHIN(expected * 0.02, expected, (double) buckets[b]);
    }
}

// the rejection only shows itself at a bound near the top of the range, where a draw
// covers each value once or twice rather than billions of times. Two thirds of 2^64 is
// the worst of them: without the rejection every other value would come up twice as
// often as its neighbour, and the parity of the result gives that away at once. Every
// small bound hides the same flaw under a bias of one part in 2^62.
static void test_rng_u64_max_rejects_where_the_range_does_not_fit() {
    static constexpr uint64_t max = (UINT64_MAX / 3) * 2;

    tda_Rng rng = tda_rng_from_seed(17);
    size_t even = 0;

    for (size_t i = 0; i < TRIALS; ++i) {
        even += (tda_rng_u64_max(&rng, max) & 1) == 0 ? 1 : 0;
    }

    const double expected = (double) TRIALS / 2.0;
    TEST_ASSERT_DOUBLE_WITHIN(expected * 0.02, expected, (double) even);
}

static void test_rng_idx_covers_every_position() {
    tda_Rng rng = tda_rng_from_seed(5);

    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_size_t(0, tda_rng_idx(&rng, 1));
    }

    size_t buckets[4] = {0};
    for (size_t i = 0; i < TRIALS; ++i) {
        const size_t idx = tda_rng_idx(&rng, 4);
        TEST_ASSERT_TRUE(idx < 4);
        ++buckets[idx];
    }

    const double expected = (double) TRIALS / 4.0;
    for (size_t b = 0; b < 4; ++b) {
        TEST_ASSERT_DOUBLE_WITHIN(expected * 0.02, expected, (double) buckets[b]);
    }
}

static void test_rng_i32_range_holds_its_ends() {
    tda_Rng rng = tda_rng_from_seed(6);

    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_INT32(-7, tda_rng_i32_range(&rng, -7, -7));
    }

    bool saw_lo = false;
    bool saw_hi = false;
    for (size_t i = 0; i < 1000; ++i) {
        const int32_t val = tda_rng_i32_range(&rng, -2, 2);
        TEST_ASSERT_TRUE(val >= -2 && val <= 2);

        saw_lo = saw_lo || val == -2;
        saw_hi = saw_hi || val == 2;
    }

    // inclusive at both ends, which is the half of the contract an off-by-one hides in
    TEST_ASSERT_TRUE(saw_lo);
    TEST_ASSERT_TRUE(saw_hi);
}

// the width of the full range overflows an int32_t and only fits unsigned, which is why
// the implementation subtracts there
static void test_rng_i32_range_survives_the_whole_width() {
    tda_Rng rng = tda_rng_from_seed(8);

    bool saw_negative = false;
    bool saw_positive = false;

    for (size_t i = 0; i < 1000; ++i) {
        const int32_t val = tda_rng_i32_range(&rng, INT32_MIN, INT32_MAX);
        saw_negative = saw_negative || val < 0;
        saw_positive = saw_positive || val > 0;
    }

    TEST_ASSERT_TRUE(saw_negative);
    TEST_ASSERT_TRUE(saw_positive);
}

static void test_rng_i64_range_survives_the_whole_width() {
    tda_Rng rng = tda_rng_from_seed(9);

    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_INT64(42, tda_rng_i64_range(&rng, 42, 42));
    }

    bool saw_negative = false;
    bool saw_positive = false;

    for (size_t i = 0; i < 1000; ++i) {
        const int64_t val = tda_rng_i64_range(&rng, INT64_MIN, INT64_MAX);
        saw_negative = saw_negative || val < 0;
        saw_positive = saw_positive || val > 0;
    }

    TEST_ASSERT_TRUE(saw_negative);
    TEST_ASSERT_TRUE(saw_positive);
}

/* ========== floats ========== */

static void test_rng_f64_stays_below_one() {
    tda_Rng rng = tda_rng_from_seed(10);

    double lowest = 1.0;
    double highest = 0.0;

    for (size_t i = 0; i < TRIALS; ++i) {
        const double val = tda_rng_f64(&rng);
        TEST_ASSERT_TRUE(val >= 0.0 && val < 1.0);

        lowest = val < lowest ? val : lowest;
        highest = val > highest ? val : highest;
    }

    // and it does reach for both ends rather than hovering in the middle
    TEST_ASSERT_TRUE(lowest < 0.001);
    TEST_ASSERT_TRUE(highest > 0.999);
}

static void test_rng_f32_stays_below_one() {
    tda_Rng rng = tda_rng_from_seed(11);

    for (size_t i = 0; i < TRIALS; ++i) {
        const float val = tda_rng_f32(&rng);
        TEST_ASSERT_TRUE(val >= 0.0f && val < 1.0f);
    }
}

static void test_rng_f64_spreads_evenly() {
    tda_Rng rng = tda_rng_from_seed(12);

    size_t buckets[10] = {0};
    for (size_t i = 0; i < TRIALS; ++i) {
        ++buckets[(size_t) (tda_rng_f64(&rng) * 10.0)];
    }

    const double expected = (double) TRIALS / 10.0;
    for (size_t b = 0; b < 10; ++b) {
        TEST_ASSERT_DOUBLE_WITHIN(expected * 0.05, expected, (double) buckets[b]);
    }
}

static void test_rng_f64_range_stays_half_open() {
    tda_Rng rng = tda_rng_from_seed(13);

    for (size_t i = 0; i < TRIALS; ++i) {
        const double val = tda_rng_f64_range(&rng, -2.5, 7.5);
        TEST_ASSERT_TRUE(val >= -2.5 && val < 7.5);
    }

    // a width no int could hold, and one narrow enough that the scaling rounds
    for (size_t i = 0; i < 1000; ++i) {
        const double wide = tda_rng_f64_range(&rng, -1e300, 1e300);
        TEST_ASSERT_TRUE(wide >= -1e300 && wide < 1e300);

        const double narrow = tda_rng_f64_range(&rng, 1.0, nextafter(1.0, 2.0));
        TEST_ASSERT_TRUE(narrow >= 1.0 && narrow < nextafter(1.0, 2.0));
    }
}

// the empty range names one value and no other, so that is what it gives back
static void test_rng_f64_range_collapses_when_the_ends_meet() {
    tda_Rng rng = tda_rng_from_seed(14);

    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_DOUBLE(3.5, tda_rng_f64_range(&rng, 3.5, 3.5));
    }
}

static void test_rng_f32_range_stays_half_open() {
    tda_Rng rng = tda_rng_from_seed(15);

    for (size_t i = 0; i < TRIALS; ++i) {
        const float val = tda_rng_f32_range(&rng, -1.0f, 1.0f);
        TEST_ASSERT_TRUE(val >= -1.0f && val < 1.0f);
    }

    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_FLOAT(0.25f, tda_rng_f32_range(&rng, 0.25f, 0.25f));
    }
}

/* ========== bool ========== */

static void test_rng_bool_is_a_fair_coin() {
    tda_Rng rng = tda_rng_from_seed(16);

    size_t heads = 0;
    size_t longest_run = 0;
    size_t run = 0;
    bool previous = false;

    for (size_t i = 0; i < TRIALS; ++i) {
        const bool val = tda_rng_bool(&rng);
        heads += val ? 1 : 0;

        run = (i > 0 && val == previous) ? run + 1 : 1;
        longest_run = run > longest_run ? run : longest_run;
        previous = val;
    }

    const double expected = (double) TRIALS / 2.0;
    TEST_ASSERT_DOUBLE_WITHIN(expected * 0.02, expected, (double) heads);

    // a run of 40 in 200000 flips is beyond astronomical, and a stuck top bit would show
    // up here rather than in the count
    TEST_ASSERT_TRUE(longest_run < 40);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_rng_matches_the_reference_stream);
    RUN_TEST(test_rng_replays_the_same_seed);
    RUN_TEST(test_rng_takes_zero_as_a_seed);
    RUN_TEST(test_rng_copies_as_a_value);

    RUN_TEST(test_rng_u32_is_the_top_half_of_a_draw);
    RUN_TEST(test_rng_u32_takes_the_full_width);
    RUN_TEST(test_rng_u64_max_stays_in_range);
    RUN_TEST(test_rng_u64_max_takes_the_full_width);
    RUN_TEST(test_rng_u32_max_stays_in_range);
    RUN_TEST(test_rng_u64_max_is_flat_over_a_bound_that_does_not_divide_the_width);
    RUN_TEST(test_rng_u64_max_rejects_where_the_range_does_not_fit);
    RUN_TEST(test_rng_idx_covers_every_position);
    RUN_TEST(test_rng_i32_range_holds_its_ends);
    RUN_TEST(test_rng_i32_range_survives_the_whole_width);
    RUN_TEST(test_rng_i64_range_survives_the_whole_width);

    RUN_TEST(test_rng_f64_stays_below_one);
    RUN_TEST(test_rng_f32_stays_below_one);
    RUN_TEST(test_rng_f64_spreads_evenly);
    RUN_TEST(test_rng_f64_range_stays_half_open);
    RUN_TEST(test_rng_f64_range_collapses_when_the_ends_meet);
    RUN_TEST(test_rng_f32_range_stays_half_open);

    RUN_TEST(test_rng_bool_is_a_fair_coin);

    return UNITY_END();
}
