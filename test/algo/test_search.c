#include "tda/algo/permute.h"
#include "tda/algo/search.h"
#include "tda/core/util.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// ordered by key only, so tag can witness which of the equal elements was picked
typedef struct {
    int32_t key;
    int32_t tag;
} Tagged;

static int cmp_tagged(const void *a, const void *b) {
    return tda_cmp_i32(&((const Tagged *) a)->key, &((const Tagged *) b)->key);
}

// a predicate that needs nothing: ctx stays null
static bool is_even(const void *elem, void *ctx) {
    TDA_UNUSED(ctx);

    return *(const int32_t *) elem % 2 == 0;
}

// a predicate parameterized through ctx — the whole point of carrying one
static bool less_than(const void *elem, void *ctx) {
    return *(const int32_t *) elem < *(const int32_t *) ctx;
}

static bool greater_than(const void *elem, void *ctx) {
    return *(const int32_t *) elem > *(const int32_t *) ctx;
}

/* ========== find ========== */

static void test_find_reports_the_first_match() {
    constexpr int32_t buf[5] = {3, 1, 2, 1, 4};
    constexpr int32_t key = 1;

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find(TDA_SPAN_FROM_DATA(int32_t, buf, 5), &key, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
}

// on a miss the out param is left untouched — the caller's value must survive
static void test_find_miss_leaves_the_out_param_alone() {
    constexpr int32_t buf[3] = {1, 2, 3};
    constexpr int32_t key = 9;

    size_t idx = 777;
    TEST_ASSERT_FALSE(tda_span_find(TDA_SPAN_FROM_DATA(int32_t, buf, 3), &key, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(777, idx);
}

static void test_find_in_an_empty_span_misses() {
    constexpr int32_t buf[2] = {1, 2};
    constexpr int32_t key = 1;

    size_t idx = 555;
    TEST_ASSERT_FALSE(tda_span_find(TDA_SPAN_FROM_DATA(int32_t, buf, 0), &key, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(555, idx);
}

static void test_find_matches_the_last_elem() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};
    constexpr int32_t key = 4;

    size_t idx = 0;
    TEST_ASSERT_TRUE(tda_span_find(TDA_SPAN_FROM_DATA(int32_t, buf, 4), &key, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(3, idx);
}

// searching a subspan reports an index relative to that subspan
static void test_find_index_is_relative_to_the_span() {
    constexpr int32_t buf[5] = {1, 2, 3, 4, 5};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 5);
    constexpr int32_t key = 4;

    size_t idx = 0;
    TEST_ASSERT_TRUE(tda_span_find(tda_span_sub(s, 2, 3), &key, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
}

/* ========== contains / count ========== */

static void test_contains() {
    constexpr int32_t buf[3] = {1, 2, 3};
    constexpr int32_t present = 2;
    constexpr int32_t absent = 9;

    TEST_ASSERT_TRUE(tda_span_contains(TDA_SPAN_FROM_DATA(int32_t, buf, 3), &present, tda_eq_i32));
    TEST_ASSERT_FALSE(tda_span_contains(TDA_SPAN_FROM_DATA(int32_t, buf, 3), &absent, tda_eq_i32));
}

static void test_count_tallies_every_match() {
    constexpr int32_t buf[6] = {1, 2, 1, 3, 1, 4};
    constexpr int32_t key = 1;

    TEST_ASSERT_EQUAL_size_t(3, tda_span_count(TDA_SPAN_FROM_DATA(int32_t, buf, 6), &key, tda_eq_i32));
}

static void test_count_of_an_absent_key_is_zero() {
    constexpr int32_t buf[3] = {1, 2, 3};
    constexpr int32_t key = 9;

    TEST_ASSERT_EQUAL_size_t(0, tda_span_count(TDA_SPAN_FROM_DATA(int32_t, buf, 3), &key, tda_eq_i32));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_count(TDA_SPAN_FROM_DATA(int32_t, buf, 0), &key, tda_eq_i32));
}

/* ========== bounds ========== */

static void test_lower_bound_stops_at_the_first_equal() {
    constexpr int32_t buf[6] = {1, 2, 2, 2, 3, 4};
    constexpr int32_t key = 2;

    TEST_ASSERT_EQUAL_size_t(1, tda_span_lower_bound(TDA_SPAN_FROM_DATA(int32_t, buf, 6), &key, tda_cmp_i32));
}

static void test_upper_bound_stops_past_the_last_equal() {
    constexpr int32_t buf[6] = {1, 2, 2, 2, 3, 4};
    constexpr int32_t key = 2;

    TEST_ASSERT_EQUAL_size_t(4, tda_span_upper_bound(TDA_SPAN_FROM_DATA(int32_t, buf, 6), &key, tda_cmp_i32));
}

// an absent key gives the insertion point, and both bounds agree on it
static void test_bounds_agree_on_an_absent_key() {
    constexpr int32_t buf[4] = {1, 3, 5, 7};
    constexpr int32_t key = 4;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 4);

    TEST_ASSERT_EQUAL_size_t(2, tda_span_lower_bound(s, &key, tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(2, tda_span_upper_bound(s, &key, tda_cmp_i32));
}

// past-the-end is len, which is also the "not found" reading of a bound
static void test_bounds_past_the_end() {
    constexpr int32_t buf[3] = {1, 2, 3};
    constexpr int32_t key = 9;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 3);

    TEST_ASSERT_EQUAL_size_t(3, tda_span_lower_bound(s, &key, tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(3, tda_span_upper_bound(s, &key, tda_cmp_i32));
}

static void test_bounds_before_the_start() {
    constexpr int32_t buf[3] = {5, 6, 7};
    constexpr int32_t key = 1;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 3);

    TEST_ASSERT_EQUAL_size_t(0, tda_span_lower_bound(s, &key, tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_upper_bound(s, &key, tda_cmp_i32));
}

static void test_bounds_on_an_empty_span() {
    constexpr int32_t buf[2] = {1, 2};
    constexpr int32_t key = 1;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 0);

    TEST_ASSERT_EQUAL_size_t(0, tda_span_lower_bound(s, &key, tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_upper_bound(s, &key, tda_cmp_i32));
}

// the span between the two bounds is exactly the run of equal elements
static void test_bounds_delimit_the_equal_run() {
    constexpr int32_t buf[7] = {1, 2, 2, 2, 2, 3, 4};
    constexpr int32_t key = 2;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 7);

    const size_t lo = tda_span_lower_bound(s, &key, tda_cmp_i32);
    const size_t hi = tda_span_upper_bound(s, &key, tda_cmp_i32);

    TEST_ASSERT_EQUAL_size_t(4, hi - lo);
    TEST_ASSERT_EQUAL_size_t(tda_span_count(s, &key, tda_eq_i32), hi - lo);
}

/* ========== binary_search ========== */

static void test_binary_search_finds_a_present_key() {
    constexpr int32_t buf[5] = {1, 3, 5, 7, 9};
    constexpr int32_t key = 7;

    size_t idx = 0;
    TEST_ASSERT_TRUE(tda_span_binary_search(TDA_SPAN_FROM_DATA(int32_t, buf, 5), &key, tda_cmp_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(3, idx);
}

// among duplicates it must report the first one, matching lower_bound
static void test_binary_search_reports_the_first_of_the_duplicates() {
    constexpr Tagged buf[5] = {{1, 0}, {2, 10}, {2, 11}, {2, 12}, {3, 0}};
    constexpr Tagged key = {2, -1};

    size_t idx = 0;
    TEST_ASSERT_TRUE(tda_span_binary_search(TDA_SPAN_FROM_DATA(Tagged, buf, 5), &key, cmp_tagged, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
    TEST_ASSERT_EQUAL_INT32(10, buf[idx].tag);
}

static void test_binary_search_miss_leaves_the_out_param_alone() {
    constexpr int32_t buf[4] = {1, 3, 5, 7};
    constexpr int32_t key = 4;

    size_t idx = 333;
    TEST_ASSERT_FALSE(tda_span_binary_search(TDA_SPAN_FROM_DATA(int32_t, buf, 4), &key, tda_cmp_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(333, idx);
}

// a key past the end drives lower_bound to len — that must not read out of range
static void test_binary_search_key_past_the_end_misses() {
    constexpr int32_t buf[3] = {1, 2, 3};
    constexpr int32_t key = 9;

    size_t idx = 111;
    TEST_ASSERT_FALSE(tda_span_binary_search(TDA_SPAN_FROM_DATA(int32_t, buf, 3), &key, tda_cmp_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(111, idx);
}

static void test_binary_search_on_an_empty_span_misses() {
    constexpr int32_t buf[2] = {1, 2};
    constexpr int32_t key = 1;

    size_t idx = 222;
    TEST_ASSERT_FALSE(tda_span_binary_search(TDA_SPAN_FROM_DATA(int32_t, buf, 0), &key, tda_cmp_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(222, idx);
}

// every element of a sorted span must be findable, including both edges
static void test_binary_search_finds_every_element() {
    constexpr int32_t buf[6] = {2, 4, 6, 8, 10, 12};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 6);

    for (size_t i = 0; i < 6; ++i) {
        const int32_t key = buf[i];
        size_t idx = 999;
        TEST_ASSERT_TRUE(tda_span_binary_search(s, &key, tda_cmp_i32, &idx));
        TEST_ASSERT_EQUAL_size_t(i, idx);
    }
}

/* ========== find_last ========== */

// every key, present or not, over a span with repeats, against a walk from the back
static void test_find_last_matches_a_walk_from_the_back() {
    constexpr int32_t buf[8] = {3, 1, 2, 1, 4, 3, 1, 5};

    for (size_t n = 0; n <= 8; ++n) {
        for (int32_t key = 0; key <= 6; ++key) {
            bool want_hit = false;
            size_t want_idx = 0;
            for (size_t i = n; i > 0; --i) {
                if (buf[i - 1] == key) {
                    want_hit = true;
                    want_idx = i - 1;
                    break;
                }
            }

            size_t idx = 999;
            TEST_ASSERT_EQUAL(want_hit, tda_span_find_last(TDA_SPAN_FROM_DATA(int32_t, buf, n), &key, tda_eq_i32, &idx));
            TEST_ASSERT_EQUAL_size_t(want_hit ? want_idx : 999, idx);
        }
    }
}

static void test_find_last_at_either_end() {
    constexpr int32_t buf[4] = {7, 1, 2, 7};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 4);

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_last(s, &(int32_t){7}, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(3, idx);

    TEST_ASSERT_TRUE(tda_span_find_last(tda_span_sub(s, 0, 3), &(int32_t){7}, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(0, idx);
}

static void test_find_last_if_reports_the_last_match() {
    constexpr int32_t buf[6] = {2, 1, 4, 3, 6, 5};

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_last_if(TDA_SPAN_FROM_DATA(int32_t, buf, 6), is_even, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(4, idx);

    int32_t bound = 2;
    TEST_ASSERT_TRUE(tda_span_find_last_if(TDA_SPAN_FROM_DATA(int32_t, buf, 6), less_than, &bound, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
}

static void test_find_last_if_miss_leaves_the_out_param_alone() {
    constexpr int32_t buf[3] = {1, 3, 5};

    size_t idx = 777;
    TEST_ASSERT_FALSE(tda_span_find_last_if(TDA_SPAN_FROM_DATA(int32_t, buf, 3), is_even, nullptr, &idx));
    TEST_ASSERT_FALSE(tda_span_find_last_if(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), is_even, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(777, idx);
}

static size_t asked[8];
static size_t asked_len = 0;

// writes down which elem it was asked about, and passes the value 'ctx' points at
static bool record_and_match(const void *elem, void *ctx) {
    asked[asked_len++] = (size_t) *(const int32_t *) elem;
    return *(const int32_t *) elem == *(const int32_t *) ctx;
}

// back to front, and no further than the first hit
static void test_find_last_if_asks_from_the_back_and_stops() {
    constexpr int32_t buf[6] = {0, 1, 2, 3, 4, 5};
    int32_t target = 3;

    asked_len = 0;
    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_last_if(TDA_SPAN_FROM_DATA(int32_t, buf, 6), record_and_match, &target, &idx));

    TEST_ASSERT_EQUAL_size_t(3, idx);
    TEST_ASSERT_EQUAL_size_t(3, asked_len);
    TEST_ASSERT_EQUAL_size_t(5, asked[0]);
    TEST_ASSERT_EQUAL_size_t(4, asked[1]);
    TEST_ASSERT_EQUAL_size_t(3, asked[2]);
}

/* ========== find_if ========== */

static void test_find_if_reports_the_first_match() {
    constexpr int32_t buf[5] = {3, 1, 4, 2, 6};

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_if(TDA_SPAN_FROM_DATA(int32_t, buf, 5), is_even, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(2, idx);
}

static void test_find_if_miss_leaves_the_out_param_alone() {
    constexpr int32_t buf[3] = {1, 3, 5};

    size_t idx = 777;
    TEST_ASSERT_FALSE(tda_span_find_if(TDA_SPAN_FROM_DATA(int32_t, buf, 3), is_even, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(777, idx);
}

static void test_find_if_on_an_empty_span_finds_nothing() {
    size_t idx = 555;
    TEST_ASSERT_FALSE(tda_span_find_if(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), is_even, nullptr, &idx));
    TEST_ASSERT_EQUAL_size_t(555, idx);
}

// ctx is what makes a predicate parameterizable at all
static void test_find_if_passes_the_ctx_through() {
    constexpr int32_t buf[5] = {1, 2, 3, 4, 5};
    int32_t bound = 3;

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_if(TDA_SPAN_FROM_DATA(int32_t, buf, 5), greater_than, &bound, &idx));
    TEST_ASSERT_EQUAL_size_t(3, idx);

    bound = 4;
    TEST_ASSERT_TRUE(tda_span_find_if(TDA_SPAN_FROM_DATA(int32_t, buf, 5), greater_than, &bound, &idx));
    TEST_ASSERT_EQUAL_size_t(4, idx);
}

static void test_count_if_counts_every_match() {
    constexpr int32_t buf[6] = {1, 2, 3, 4, 5, 6};

    TEST_ASSERT_EQUAL_size_t(3, tda_span_count_if(TDA_SPAN_FROM_DATA(int32_t, buf, 6), is_even, nullptr));
}

static void test_count_if_of_none_is_zero() {
    constexpr int32_t buf[3] = {1, 3, 5};

    TEST_ASSERT_EQUAL_size_t(0, tda_span_count_if(TDA_SPAN_FROM_DATA(int32_t, buf, 3), is_even, nullptr));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_count_if(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), is_even, nullptr));
}

/* ========== partition_point ========== */

static void test_partition_point_finds_the_boundary() {
    constexpr int32_t buf[6] = {2, 4, 6, 1, 3, 5};

    TEST_ASSERT_EQUAL_size_t(3, tda_span_partition_point(TDA_SPAN_FROM_DATA(int32_t, buf, 6), is_even, nullptr));
}

static void test_partition_point_at_the_ends_and_on_empty() {
    constexpr int32_t all[3] = {2, 4, 6};
    constexpr int32_t none[3] = {1, 3, 5};

    TEST_ASSERT_EQUAL_size_t(3, tda_span_partition_point(TDA_SPAN_FROM_DATA(int32_t, all, 3), is_even, nullptr));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_partition_point(TDA_SPAN_FROM_DATA(int32_t, none, 3), is_even, nullptr));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_partition_point(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), is_even, nullptr));
}

// it is a binary search, so it must land where a linear scan would
static void test_partition_point_matches_a_linear_scan() {
    constexpr int32_t buf[9] = {20, 18, 15, 12, 9, 7, 4, 2, 1};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 9);

    for (int32_t bound = 0; bound <= 21; ++bound) {
        size_t linear = 0;
        while (linear < 9 && greater_than(&buf[linear], &bound)) {
            ++linear;
        }

        TEST_ASSERT_EQUAL_size_t(linear, tda_span_partition_point(s, greater_than, &bound));
    }
}

// with "less than key" over a sorted span it is lower_bound, by construction
static void test_partition_point_generalizes_lower_bound() {
    constexpr int32_t buf[7] = {1, 2, 2, 2, 3, 4, 5};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 7);

    for (int32_t key = 0; key <= 6; ++key) {
        TEST_ASSERT_EQUAL_size_t(
            tda_span_lower_bound(s, &key, tda_cmp_i32),
            tda_span_partition_point(s, less_than, &key)
        );
    }
}

// what tda_span_partition returns is where partition_point then lands
static void test_partition_point_agrees_with_partition() {
    int32_t buf[7] = {7, 2, 9, 4, 1, 6, 3};
    const tda_SpanMut m = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7);

    const size_t boundary = tda_span_partition(m, is_even, nullptr);

    TEST_ASSERT_EQUAL_size_t(boundary, tda_span_partition_point(tda_span_mut_to_span(m), is_even, nullptr));
}

/* ========== all_of / any_of / none_of ========== */

static void test_all_of_holds_only_when_every_elem_satisfies() {
    constexpr int32_t all[3] = {2, 4, 6};
    constexpr int32_t one_odd[3] = {2, 4, 5};

    TEST_ASSERT_TRUE(tda_span_all_of(TDA_SPAN_FROM_DATA(int32_t, all, 3), is_even, nullptr));
    TEST_ASSERT_FALSE(tda_span_all_of(TDA_SPAN_FROM_DATA(int32_t, one_odd, 3), is_even, nullptr));
}

static void test_any_of_holds_when_at_least_one_satisfies() {
    constexpr int32_t one_even[3] = {1, 3, 4};
    constexpr int32_t none_even[3] = {1, 3, 5};

    TEST_ASSERT_TRUE(tda_span_any_of(TDA_SPAN_FROM_DATA(int32_t, one_even, 3), is_even, nullptr));
    TEST_ASSERT_FALSE(tda_span_any_of(TDA_SPAN_FROM_DATA(int32_t, none_even, 3), is_even, nullptr));
}

static void test_none_of_is_the_negation_of_any_of() {
    constexpr int32_t buf[3] = {1, 3, 5};

    TEST_ASSERT_TRUE(tda_span_none_of(TDA_SPAN_FROM_DATA(int32_t, buf, 3), is_even, nullptr));
    TEST_ASSERT_FALSE(tda_span_none_of(TDA_SPAN_FROM_DATA(int32_t, buf, 3), greater_than, &(int32_t){2}));
}

// the empty span: all_of is vacuously true, any_of false, none_of true
static void test_the_quantifiers_agree_on_an_empty_span() {
    const tda_Span empty = TDA_SPAN_FROM_DATA(int32_t, nullptr, 0);

    TEST_ASSERT_TRUE(tda_span_all_of(empty, is_even, nullptr));
    TEST_ASSERT_FALSE(tda_span_any_of(empty, is_even, nullptr));
    TEST_ASSERT_TRUE(tda_span_none_of(empty, is_even, nullptr));
}

static size_t cmp_calls = 0;

static int cmp_counting_i32(const void *a, const void *b) {
    ++cmp_calls;

    return tda_cmp_i32(a, b);
}

/* ========== equal_range ========== */

static void test_equal_range_covers_the_whole_run() {
    constexpr int32_t buf[7] = {1, 2, 2, 2, 3, 4, 5};
    constexpr int32_t key = 2;

    const tda_Range r = tda_span_equal_range(TDA_SPAN_FROM_DATA(int32_t, buf, 7), &key, tda_cmp_i32);

    TEST_ASSERT_EQUAL_size_t(1, r.lo);
    TEST_ASSERT_EQUAL_size_t(4, r.hi);
}

// the two ends must agree with the bounds taken separately
static void test_equal_range_agrees_with_the_bounds() {
    constexpr int32_t buf[7] = {1, 2, 2, 2, 3, 4, 5};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 7);

    for (int32_t key = 0; key <= 6; ++key) {
        const tda_Range r = tda_span_equal_range(s, &key, tda_cmp_i32);

        TEST_ASSERT_EQUAL_size_t(tda_span_lower_bound(s, &key, tda_cmp_i32), r.lo);
        TEST_ASSERT_EQUAL_size_t(tda_span_upper_bound(s, &key, tda_cmp_i32), r.hi);
    }
}

// a missing key gives an empty range sitting where it would be inserted
static void test_equal_range_of_a_missing_key_is_empty() {
    constexpr int32_t buf[4] = {1, 3, 5, 7};
    constexpr int32_t key = 4;

    const tda_Range r = tda_span_equal_range(TDA_SPAN_FROM_DATA(int32_t, buf, 4), &key, tda_cmp_i32);

    TEST_ASSERT_EQUAL_size_t(2, r.lo);
    TEST_ASSERT_EQUAL_size_t(2, r.hi);
}

static void test_equal_range_at_the_ends_and_on_empty() {
    constexpr int32_t buf[3] = {2, 2, 2};
    constexpr int32_t low = 1;
    constexpr int32_t high = 9;
    constexpr int32_t all = 2;

    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 3);

    tda_Range r = tda_span_equal_range(s, &low, tda_cmp_i32);
    TEST_ASSERT_EQUAL_size_t(0, r.lo);
    TEST_ASSERT_EQUAL_size_t(0, r.hi);

    r = tda_span_equal_range(s, &high, tda_cmp_i32);
    TEST_ASSERT_EQUAL_size_t(3, r.lo);
    TEST_ASSERT_EQUAL_size_t(3, r.hi);

    r = tda_span_equal_range(s, &all, tda_cmp_i32);
    TEST_ASSERT_EQUAL_size_t(0, r.lo);
    TEST_ASSERT_EQUAL_size_t(3, r.hi);

    r = tda_span_equal_range(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), &all, tda_cmp_i32);
    TEST_ASSERT_EQUAL_size_t(0, r.lo);
    TEST_ASSERT_EQUAL_size_t(0, r.hi);
}

// the second descent is over the tail after the lower bound, not over the
// whole span again — invisible in the answer, visible in the comparisons
static void test_equal_range_does_not_search_the_whole_span_twice() {
    constexpr int32_t buf[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    constexpr int32_t key = 14;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 16);

    cmp_calls = 0;
    (void) tda_span_lower_bound(s, &key, cmp_counting_i32);
    const size_t one_full_descent = cmp_calls;

    cmp_calls = 0;
    const tda_Range r = tda_span_equal_range(s, &key, cmp_counting_i32);

    TEST_ASSERT_EQUAL_size_t(14, r.lo);
    TEST_ASSERT_EQUAL_size_t(15, r.hi);
    TEST_ASSERT_LESS_THAN_size_t(2 * one_full_descent, cmp_calls);
}

/* ========== extremes ========== */

static void test_min_and_max_elem() {
    constexpr int32_t buf[5] = {3, 1, 4, 1, 5};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 5);

    TEST_ASSERT_EQUAL_size_t(1, tda_span_min_elem(s, tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(4, tda_span_max_elem(s, tda_cmp_i32));
}

// ties resolve to the earliest index, for both ends
static void test_extremes_pick_the_first_of_equals() {
    constexpr Tagged buf[4] = {{1, 10}, {5, 20}, {1, 11}, {5, 21}};
    const tda_Span s = TDA_SPAN_FROM_DATA(Tagged, buf, 4);

    TEST_ASSERT_EQUAL_size_t(0, tda_span_min_elem(s, cmp_tagged));
    TEST_ASSERT_EQUAL_size_t(1, tda_span_max_elem(s, cmp_tagged));
}

static void test_extremes_of_a_single_elem() {
    constexpr int32_t buf[1] = {42};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 1);

    TEST_ASSERT_EQUAL_size_t(0, tda_span_min_elem(s, tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_max_elem(s, tda_cmp_i32));
}

static void test_extremes_at_the_edges() {
    constexpr int32_t ascending[4] = {1, 2, 3, 4};
    constexpr int32_t descending[4] = {4, 3, 2, 1};

    TEST_ASSERT_EQUAL_size_t(0, tda_span_min_elem(TDA_SPAN_FROM_DATA(int32_t, ascending, 4), tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(3, tda_span_max_elem(TDA_SPAN_FROM_DATA(int32_t, ascending, 4), tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(3, tda_span_min_elem(TDA_SPAN_FROM_DATA(int32_t, descending, 4), tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_max_elem(TDA_SPAN_FROM_DATA(int32_t, descending, 4), tda_cmp_i32));
}

/* ========== minmax_elem ========== */

static void test_minmax_elem_finds_both_ends() {
    constexpr int32_t buf[5] = {3, 1, 4, 5, 2};

    const tda_MinMax mm = tda_span_minmax_elem(TDA_SPAN_FROM_DATA(int32_t, buf, 5), tda_cmp_i32);

    TEST_ASSERT_EQUAL_size_t(1, mm.min);
    TEST_ASSERT_EQUAL_size_t(3, mm.max);
}

// one pass must give exactly what the two separate passes give
static void test_minmax_elem_agrees_with_min_and_max() {
    constexpr int32_t buf[6] = {5, 5, 1, 9, 1, 9};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 6);

    const tda_MinMax mm = tda_span_minmax_elem(s, tda_cmp_i32);

    TEST_ASSERT_EQUAL_size_t(tda_span_min_elem(s, tda_cmp_i32), mm.min);
    TEST_ASSERT_EQUAL_size_t(tda_span_max_elem(s, tda_cmp_i32), mm.max);
}

// ties go to the FIRST elem on both sides, unlike std::minmax_element
static void test_minmax_elem_breaks_ties_towards_the_front() {
    constexpr int32_t buf[4] = {2, 2, 2, 2};

    const tda_MinMax mm = tda_span_minmax_elem(TDA_SPAN_FROM_DATA(int32_t, buf, 4), tda_cmp_i32);

    TEST_ASSERT_EQUAL_size_t(0, mm.min);
    TEST_ASSERT_EQUAL_size_t(0, mm.max);
}

static void test_minmax_elem_of_a_single_elem() {
    constexpr int32_t buf[1] = {7};

    const tda_MinMax mm = tda_span_minmax_elem(TDA_SPAN_FROM_DATA(int32_t, buf, 1), tda_cmp_i32);

    TEST_ASSERT_EQUAL_size_t(0, mm.min);
    TEST_ASSERT_EQUAL_size_t(0, mm.max);
}

/* ========== find_sub / find_last_sub ========== */

// the old name answers as the new one does, until 2.0 removes it
static void test_find_sub_last_is_find_last_sub() {
    constexpr int32_t buf[8] = {5, 1, 2, 3, 1, 2, 3, 9};
    constexpr int32_t pat[2] = {2, 3};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 8);
    const tda_Span sub = TDA_SPAN_FROM_DATA(int32_t, pat, 2);

    size_t old_idx = 0;
    size_t new_idx = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    TEST_ASSERT_TRUE(tda_span_find_sub_last(s, sub, tda_eq_i32, &old_idx));
#pragma GCC diagnostic pop
    TEST_ASSERT_TRUE(tda_span_find_last_sub(s, sub, tda_eq_i32, &new_idx));

    TEST_ASSERT_EQUAL_size_t(5, new_idx);
    TEST_ASSERT_EQUAL_size_t(new_idx, old_idx);
}

static void test_find_sub_reports_the_first_occurrence() {
    constexpr int32_t buf[8] = {5, 1, 2, 3, 1, 2, 3, 9};
    constexpr int32_t pat[3] = {1, 2, 3};

    size_t idx = 999;
    TEST_ASSERT_TRUE(
        tda_span_find_sub(TDA_SPAN_FROM_DATA(int32_t, buf, 8), TDA_SPAN_FROM_DATA(int32_t, pat, 3), tda_eq_i32, &idx)
    );
    TEST_ASSERT_EQUAL_size_t(1, idx);
}

static void test_find_last_sub_reports_the_last_occurrence() {
    constexpr int32_t buf[8] = {5, 1, 2, 3, 1, 2, 3, 9};
    constexpr int32_t pat[3] = {1, 2, 3};

    size_t idx = 999;
    TEST_ASSERT_TRUE(
        tda_span_find_last_sub(
            TDA_SPAN_FROM_DATA(int32_t, buf, 8),
            TDA_SPAN_FROM_DATA(int32_t, pat, 3),
            tda_eq_i32,
            &idx
        )
    );
    TEST_ASSERT_EQUAL_size_t(4, idx);
}

// a single occurrence is both the first and the last one
static void test_the_two_sub_finders_agree_on_a_unique_occurrence() {
    constexpr int32_t buf[6] = {8, 8, 1, 2, 8, 8};
    constexpr int32_t pat[2] = {1, 2};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 6);
    const tda_Span sub = TDA_SPAN_FROM_DATA(int32_t, pat, 2);

    size_t first = 0, last = 0;
    TEST_ASSERT_TRUE(tda_span_find_sub(s, sub, tda_eq_i32, &first));
    TEST_ASSERT_TRUE(tda_span_find_last_sub(s, sub, tda_eq_i32, &last));
    TEST_ASSERT_EQUAL_size_t(2, first);
    TEST_ASSERT_EQUAL_size_t(first, last);
}

// overlapping occurrences are occurrences: aa sits at 0, 1 and 2 in aaaa
static void test_the_sub_finders_see_overlapping_occurrences() {
    constexpr int32_t buf[4] = {7, 7, 7, 7};
    constexpr int32_t pat[2] = {7, 7};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 4);
    const tda_Span sub = TDA_SPAN_FROM_DATA(int32_t, pat, 2);

    size_t first = 0, last = 0;
    TEST_ASSERT_TRUE(tda_span_find_sub(s, sub, tda_eq_i32, &first));
    TEST_ASSERT_TRUE(tda_span_find_last_sub(s, sub, tda_eq_i32, &last));
    TEST_ASSERT_EQUAL_size_t(0, first);
    TEST_ASSERT_EQUAL_size_t(2, last);
}

// a partial prefix must not be reported: the match has to run to the end of sub
static void test_find_sub_does_not_settle_for_a_prefix() {
    constexpr int32_t buf[5] = {1, 2, 9, 1, 2};
    constexpr int32_t pat[3] = {1, 2, 3};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 5);
    const tda_Span sub = TDA_SPAN_FROM_DATA(int32_t, pat, 3);

    size_t idx = 111;
    TEST_ASSERT_FALSE(tda_span_find_sub(s, sub, tda_eq_i32, &idx));
    TEST_ASSERT_FALSE(tda_span_find_last_sub(s, sub, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(111, idx);
}

// a sub that runs to the very end still fits — the loop must not stop one short
static void test_the_sub_finders_match_at_the_end() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};
    constexpr int32_t pat[2] = {3, 4};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 4);
    const tda_Span sub = TDA_SPAN_FROM_DATA(int32_t, pat, 2);

    size_t first = 0, last = 0;
    TEST_ASSERT_TRUE(tda_span_find_sub(s, sub, tda_eq_i32, &first));
    TEST_ASSERT_TRUE(tda_span_find_last_sub(s, sub, tda_eq_i32, &last));
    TEST_ASSERT_EQUAL_size_t(2, first);
    TEST_ASSERT_EQUAL_size_t(2, last);
}

// the whole span is one of its own subspans
static void test_the_sub_finders_match_the_whole_span() {
    constexpr int32_t buf[3] = {1, 2, 3};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 3);

    size_t first = 9, last = 9;
    TEST_ASSERT_TRUE(tda_span_find_sub(s, s, tda_eq_i32, &first));
    TEST_ASSERT_TRUE(tda_span_find_last_sub(s, s, tda_eq_i32, &last));
    TEST_ASSERT_EQUAL_size_t(0, first);
    TEST_ASSERT_EQUAL_size_t(0, last);
}

// an empty sub occurs everywhere, so the two finders answer at opposite ends
static void test_an_empty_sub_is_found_at_both_ends() {
    constexpr int32_t buf[3] = {1, 2, 3};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 3);
    const tda_Span empty = TDA_SPAN_FROM_DATA(int32_t, buf, 0);

    size_t first = 9, last = 9;
    TEST_ASSERT_TRUE(tda_span_find_sub(s, empty, tda_eq_i32, &first));
    TEST_ASSERT_TRUE(tda_span_find_last_sub(s, empty, tda_eq_i32, &last));
    TEST_ASSERT_EQUAL_size_t(0, first);
    TEST_ASSERT_EQUAL_size_t(3, last);
}

// a sub longer than the span cannot fit — and the length check must come before
// any indexing, hence the empty span too
static void test_the_sub_finders_miss_when_sub_does_not_fit() {
    constexpr int32_t buf[2] = {1, 2};
    constexpr int32_t pat[3] = {1, 2, 3};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 2);
    const tda_Span sub = TDA_SPAN_FROM_DATA(int32_t, pat, 3);
    const tda_Span empty = TDA_SPAN_FROM_DATA(int32_t, buf, 0);

    size_t idx = 222;
    TEST_ASSERT_FALSE(tda_span_find_sub(s, sub, tda_eq_i32, &idx));
    TEST_ASSERT_FALSE(tda_span_find_last_sub(s, sub, tda_eq_i32, &idx));
    TEST_ASSERT_FALSE(tda_span_find_sub(empty, sub, tda_eq_i32, &idx));
    TEST_ASSERT_FALSE(tda_span_find_last_sub(empty, sub, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(222, idx);
}

// a one elem sub is the same question tda_span_find answers
static void test_find_sub_of_one_elem_agrees_with_find() {
    constexpr int32_t buf[6] = {4, 5, 6, 5, 4, 5};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 6);

    for (int32_t key = 3; key <= 7; ++key) {
        const tda_Span sub = TDA_SPAN_FROM_DATA(int32_t, &key, 1);

        size_t by_find = 99, by_sub = 88;
        const bool hit = tda_span_find(s, &key, tda_eq_i32, &by_find);

        TEST_ASSERT_EQUAL(hit, tda_span_find_sub(s, sub, tda_eq_i32, &by_sub));
        if (hit) {
            TEST_ASSERT_EQUAL_size_t(by_find, by_sub);
        }
    }
}

/* ========== find_run ========== */

static void test_find_run_reports_the_start_of_the_first_long_enough_run() {
    constexpr int32_t buf[9] = {1, 0, 1, 1, 0, 1, 1, 1, 0};
    constexpr int32_t key = 1;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 9);

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_run(s, &key, 3, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(5, idx);
}

// a shorter demand is met earlier — the run of two comes before the run of three
static void test_find_run_of_two_stops_at_the_earlier_run() {
    constexpr int32_t buf[9] = {1, 0, 1, 1, 0, 1, 1, 1, 0};
    constexpr int32_t key = 1;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 9);

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_run(s, &key, 2, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(2, idx);
}

// the run has to be consecutive: three scattered matches are not a run of three
static void test_find_run_does_not_add_up_scattered_matches() {
    constexpr int32_t buf[6] = {1, 0, 1, 0, 1, 0};
    constexpr int32_t key = 1;

    size_t idx = 333;
    TEST_ASSERT_FALSE(tda_span_find_run(TDA_SPAN_FROM_DATA(int32_t, buf, 6), &key, 2, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(333, idx);
}

// a run of one is a single match, so it must agree with find
static void test_find_run_of_one_agrees_with_find() {
    constexpr int32_t buf[5] = {4, 5, 6, 5, 4};
    constexpr int32_t key = 5;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 5);

    size_t by_find = 0, by_run = 0;
    TEST_ASSERT_TRUE(tda_span_find(s, &key, tda_eq_i32, &by_find));
    TEST_ASSERT_TRUE(tda_span_find_run(s, &key, 1, tda_eq_i32, &by_run));
    TEST_ASSERT_EQUAL_size_t(by_find, by_run);
}

// a run of zero is demanded of nothing, so it is found at once — even on an empty span
static void test_find_run_of_zero_is_found_at_the_front() {
    constexpr int32_t buf[3] = {1, 2, 3};
    constexpr int32_t key = 9;

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_run(TDA_SPAN_FROM_DATA(int32_t, buf, 3), &key, 0, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(0, idx);

    idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_run(TDA_SPAN_FROM_DATA(int32_t, buf, 0), &key, 0, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(0, idx);
}

// a run reaching the last elem is still a run, and one longer than the span never is
static void test_find_run_at_the_end_and_longer_than_the_span() {
    constexpr int32_t buf[4] = {0, 1, 1, 1};
    constexpr int32_t key = 1;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 4);

    size_t idx = 444;
    TEST_ASSERT_TRUE(tda_span_find_run(s, &key, 3, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);

    idx = 444;
    TEST_ASSERT_FALSE(tda_span_find_run(s, &key, 5, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(444, idx);
}

// a run of n implies a run of every shorter length, at an index no later
static void test_find_run_is_monotonic_in_n() {
    constexpr int32_t buf[10] = {2, 1, 1, 2, 1, 1, 1, 1, 2, 1};
    constexpr int32_t key = 1;
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 10);

    size_t prev = 0;
    TEST_ASSERT_TRUE(tda_span_find_run(s, &key, 1, tda_eq_i32, &prev));

    for (size_t n = 2; n <= 4; ++n) {
        size_t idx = 0;
        TEST_ASSERT_TRUE(tda_span_find_run(s, &key, n, tda_eq_i32, &idx));
        TEST_ASSERT_TRUE(idx >= prev);
        prev = idx;
    }

    size_t idx = 555;
    TEST_ASSERT_FALSE(tda_span_find_run(s, &key, 5, tda_eq_i32, &idx));
}

/* ========== find_any_of ========== */

static void test_find_any_of_reports_the_first_elem_from_the_set() {
    constexpr int32_t buf[6] = {7, 8, 3, 9, 2, 3};
    constexpr int32_t set[3] = {2, 3, 4};

    size_t idx = 999;
    TEST_ASSERT_TRUE(
        tda_span_find_any_of(TDA_SPAN_FROM_DATA(int32_t, buf, 6), TDA_SPAN_FROM_DATA(int32_t, set, 3), tda_eq_i32, &idx)
    );
    TEST_ASSERT_EQUAL_size_t(2, idx);
}

// position in the set says nothing: the earliest elem of the SPAN wins
static void test_find_any_of_scans_the_span_not_the_set() {
    constexpr int32_t buf[4] = {5, 6, 7, 8};
    constexpr int32_t set[3] = {8, 7, 6};

    size_t idx = 999;
    TEST_ASSERT_TRUE(
        tda_span_find_any_of(TDA_SPAN_FROM_DATA(int32_t, buf, 4), TDA_SPAN_FROM_DATA(int32_t, set, 3), tda_eq_i32, &idx)
    );
    TEST_ASSERT_EQUAL_size_t(1, idx);
}

static void test_find_any_of_misses_when_nothing_is_shared() {
    constexpr int32_t buf[3] = {1, 2, 3};
    constexpr int32_t set[2] = {4, 5};

    size_t idx = 666;
    TEST_ASSERT_FALSE(
        tda_span_find_any_of(TDA_SPAN_FROM_DATA(int32_t, buf, 3), TDA_SPAN_FROM_DATA(int32_t, set, 2), tda_eq_i32, &idx)
    );
    TEST_ASSERT_EQUAL_size_t(666, idx);
}

// nothing is shared with an empty set, and an empty span shares nothing
static void test_find_any_of_on_empty_sides() {
    constexpr int32_t buf[3] = {1, 2, 3};
    constexpr int32_t set[2] = {1, 2};

    size_t idx = 777;
    TEST_ASSERT_FALSE(
        tda_span_find_any_of(TDA_SPAN_FROM_DATA(int32_t, buf, 3), TDA_SPAN_FROM_DATA(int32_t, set, 0), tda_eq_i32, &idx)
    );
    TEST_ASSERT_FALSE(
        tda_span_find_any_of(TDA_SPAN_FROM_DATA(int32_t, buf, 0), TDA_SPAN_FROM_DATA(int32_t, set, 2), tda_eq_i32, &idx)
    );
    TEST_ASSERT_EQUAL_size_t(777, idx);
}

// a one elem set is the same question find answers
static void test_find_any_of_a_single_elem_agrees_with_find() {
    constexpr int32_t buf[5] = {9, 8, 7, 8, 9};
    constexpr int32_t set[1] = {8};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 5);

    size_t by_find = 0, by_any = 0;
    TEST_ASSERT_TRUE(tda_span_find(s, &set[0], tda_eq_i32, &by_find));
    TEST_ASSERT_TRUE(tda_span_find_any_of(s, TDA_SPAN_FROM_DATA(int32_t, set, 1), tda_eq_i32, &by_any));
    TEST_ASSERT_EQUAL_size_t(by_find, by_any);
}

/* ========== find_adjacent ========== */

static void test_find_adjacent_reports_the_first_equal_pair() {
    constexpr int32_t buf[7] = {1, 2, 3, 3, 4, 5, 5};

    size_t idx = 999;
    TEST_ASSERT_TRUE(tda_span_find_adjacent(TDA_SPAN_FROM_DATA(int32_t, buf, 7), tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(2, idx);
}

// equal but not neighbours is not a pair
static void test_find_adjacent_misses_when_no_two_neighbours_are_equal() {
    constexpr int32_t buf[5] = {1, 2, 1, 2, 1};

    size_t idx = 888;
    TEST_ASSERT_FALSE(tda_span_find_adjacent(TDA_SPAN_FROM_DATA(int32_t, buf, 5), tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(888, idx);
}

// a pair needs two elems, so nothing shorter can hold one
static void test_find_adjacent_on_the_short_spans() {
    constexpr int32_t buf[2] = {4, 4};

    size_t idx = 999;
    TEST_ASSERT_FALSE(tda_span_find_adjacent(TDA_SPAN_FROM_DATA(int32_t, buf, 0), tda_eq_i32, &idx));
    TEST_ASSERT_FALSE(tda_span_find_adjacent(TDA_SPAN_FROM_DATA(int32_t, buf, 1), tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(999, idx);

    TEST_ASSERT_TRUE(tda_span_find_adjacent(TDA_SPAN_FROM_DATA(int32_t, buf, 2), tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(0, idx);
}

// a pair at the very end must be seen — the scan must not stop one short
static void test_find_adjacent_sees_a_pair_at_the_end() {
    constexpr int32_t buf[4] = {1, 2, 3, 3};

    size_t idx = 0;
    TEST_ASSERT_TRUE(tda_span_find_adjacent(TDA_SPAN_FROM_DATA(int32_t, buf, 4), tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(2, idx);
}

// the same question as a run of two, so the two must answer alike
static void test_find_adjacent_agrees_with_a_run_of_two() {
    constexpr int32_t buf[8] = {5, 1, 2, 2, 2, 7, 7, 0};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 8);
    constexpr int32_t key = 2;

    size_t by_adj = 0, by_run = 0;
    TEST_ASSERT_TRUE(tda_span_find_adjacent(s, tda_eq_i32, &by_adj));
    TEST_ASSERT_TRUE(tda_span_find_run(s, &key, 2, tda_eq_i32, &by_run));
    TEST_ASSERT_EQUAL_size_t(2, by_adj);
    TEST_ASSERT_EQUAL_size_t(by_adj, by_run);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_find_reports_the_first_match);
    RUN_TEST(test_find_miss_leaves_the_out_param_alone);
    RUN_TEST(test_find_in_an_empty_span_misses);
    RUN_TEST(test_find_matches_the_last_elem);
    RUN_TEST(test_find_index_is_relative_to_the_span);
    RUN_TEST(test_find_last_matches_a_walk_from_the_back);
    RUN_TEST(test_find_last_at_either_end);
    RUN_TEST(test_find_last_if_reports_the_last_match);
    RUN_TEST(test_find_last_if_miss_leaves_the_out_param_alone);
    RUN_TEST(test_find_last_if_asks_from_the_back_and_stops);

    RUN_TEST(test_find_sub_reports_the_first_occurrence);
    RUN_TEST(test_find_last_sub_reports_the_last_occurrence);
    RUN_TEST(test_find_sub_last_is_find_last_sub);
    RUN_TEST(test_the_two_sub_finders_agree_on_a_unique_occurrence);
    RUN_TEST(test_the_sub_finders_see_overlapping_occurrences);
    RUN_TEST(test_find_sub_does_not_settle_for_a_prefix);
    RUN_TEST(test_the_sub_finders_match_at_the_end);
    RUN_TEST(test_the_sub_finders_match_the_whole_span);
    RUN_TEST(test_an_empty_sub_is_found_at_both_ends);
    RUN_TEST(test_the_sub_finders_miss_when_sub_does_not_fit);
    RUN_TEST(test_find_sub_of_one_elem_agrees_with_find);

    RUN_TEST(test_find_run_reports_the_start_of_the_first_long_enough_run);
    RUN_TEST(test_find_run_of_two_stops_at_the_earlier_run);
    RUN_TEST(test_find_run_does_not_add_up_scattered_matches);
    RUN_TEST(test_find_run_of_one_agrees_with_find);
    RUN_TEST(test_find_run_of_zero_is_found_at_the_front);
    RUN_TEST(test_find_run_at_the_end_and_longer_than_the_span);
    RUN_TEST(test_find_run_is_monotonic_in_n);

    RUN_TEST(test_find_any_of_reports_the_first_elem_from_the_set);
    RUN_TEST(test_find_any_of_scans_the_span_not_the_set);
    RUN_TEST(test_find_any_of_misses_when_nothing_is_shared);
    RUN_TEST(test_find_any_of_on_empty_sides);
    RUN_TEST(test_find_any_of_a_single_elem_agrees_with_find);

    RUN_TEST(test_find_adjacent_reports_the_first_equal_pair);
    RUN_TEST(test_find_adjacent_misses_when_no_two_neighbours_are_equal);
    RUN_TEST(test_find_adjacent_on_the_short_spans);
    RUN_TEST(test_find_adjacent_sees_a_pair_at_the_end);
    RUN_TEST(test_find_adjacent_agrees_with_a_run_of_two);

    RUN_TEST(test_contains);
    RUN_TEST(test_count_tallies_every_match);
    RUN_TEST(test_count_of_an_absent_key_is_zero);

    RUN_TEST(test_lower_bound_stops_at_the_first_equal);
    RUN_TEST(test_upper_bound_stops_past_the_last_equal);
    RUN_TEST(test_bounds_agree_on_an_absent_key);
    RUN_TEST(test_bounds_past_the_end);
    RUN_TEST(test_bounds_before_the_start);
    RUN_TEST(test_bounds_on_an_empty_span);
    RUN_TEST(test_bounds_delimit_the_equal_run);

    RUN_TEST(test_binary_search_finds_a_present_key);
    RUN_TEST(test_binary_search_reports_the_first_of_the_duplicates);
    RUN_TEST(test_binary_search_miss_leaves_the_out_param_alone);
    RUN_TEST(test_binary_search_key_past_the_end_misses);
    RUN_TEST(test_binary_search_on_an_empty_span_misses);
    RUN_TEST(test_binary_search_finds_every_element);

    RUN_TEST(test_min_and_max_elem);
    RUN_TEST(test_extremes_pick_the_first_of_equals);
    RUN_TEST(test_extremes_of_a_single_elem);
    RUN_TEST(test_extremes_at_the_edges);

    RUN_TEST(test_find_if_reports_the_first_match);
    RUN_TEST(test_find_if_miss_leaves_the_out_param_alone);
    RUN_TEST(test_find_if_on_an_empty_span_finds_nothing);
    RUN_TEST(test_find_if_passes_the_ctx_through);
    RUN_TEST(test_count_if_counts_every_match);
    RUN_TEST(test_count_if_of_none_is_zero);

    RUN_TEST(test_all_of_holds_only_when_every_elem_satisfies);
    RUN_TEST(test_any_of_holds_when_at_least_one_satisfies);
    RUN_TEST(test_none_of_is_the_negation_of_any_of);
    RUN_TEST(test_the_quantifiers_agree_on_an_empty_span);


    RUN_TEST(test_equal_range_covers_the_whole_run);
    RUN_TEST(test_equal_range_agrees_with_the_bounds);
    RUN_TEST(test_equal_range_of_a_missing_key_is_empty);
    RUN_TEST(test_equal_range_at_the_ends_and_on_empty);
    RUN_TEST(test_equal_range_does_not_search_the_whole_span_twice);

    RUN_TEST(test_minmax_elem_finds_both_ends);
    RUN_TEST(test_minmax_elem_agrees_with_min_and_max);
    RUN_TEST(test_minmax_elem_breaks_ties_towards_the_front);
    RUN_TEST(test_minmax_elem_of_a_single_elem);

    RUN_TEST(test_partition_point_finds_the_boundary);
    RUN_TEST(test_partition_point_at_the_ends_and_on_empty);
    RUN_TEST(test_partition_point_matches_a_linear_scan);
    RUN_TEST(test_partition_point_generalizes_lower_bound);
    RUN_TEST(test_partition_point_agrees_with_partition);

    return UNITY_END();
}
