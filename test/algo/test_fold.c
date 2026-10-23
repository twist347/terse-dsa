#include "tda/algo/fold.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

// the accumulator is wider than the elems, which is why the caller owns it
static void sum_i32_into_i64(void *acc, const void *elem, void *) {
    *(int64_t *) acc += *(const int32_t *) elem;
}

static void product_i32(void *acc, const void *elem, void *) {
    *(int32_t *) acc *= *(const int32_t *) elem;
}

// acc = acc * 10 + elem: folding the digits left and right builds two
// different numbers, so the direction is observable. Subtraction would
// not do: taking every elem away gives the same value in any order
static void horner_i32(void *acc, const void *elem, void *) {
    *(int32_t *) acc = *(int32_t *) acc * 10 + *(const int32_t *) elem;
}

// records the order elems arrive in
static void append_digit(void *acc, const void *elem, void *) {
    char *out = acc;
    const size_t n = strlen(out);
    out[n] = (char) ('0' + *(const int32_t *) elem);
    out[n + 1] = '\0';
}

// ctx as the parameter of the fold: count elems above a bound
static void count_above(void *acc, const void *elem, void *ctx) {
    if (*(const int32_t *) elem > *(const int32_t *) ctx) {
        ++*(size_t *) acc;
    }
}

static void sum_pair_a(void *acc, const void *elem, void *) {
    *(int64_t *) acc += ((const Pair *) elem)->a;
}

static void add_i32(void *dst, const void *a, const void *b, void *) {
    *(int32_t *) dst = *(const int32_t *) a + *(const int32_t *) b;
}

static void sub_i32(void *dst, const void *a, const void *b, void *) {
    *(int32_t *) dst = *(const int32_t *) a - *(const int32_t *) b;
}

// ctx as the operation's parameter: sum, then clamp to a ceiling
static void add_capped(void *dst, const void *a, const void *b, void *ctx) {
    const int32_t sum = *(const int32_t *) a + *(const int32_t *) b;
    const int32_t cap = *(const int32_t *) ctx;

    *(int32_t *) dst = sum > cap ? cap : sum;
}

static void add_pairs(void *dst, const void *a, const void *b, void *) {
    ((Pair *) dst)->a = ((const Pair *) a)->a + ((const Pair *) b)->a;
    ((Pair *) dst)->b = ((const Pair *) a)->b + ((const Pair *) b)->b;
}

/* ========== fold ========== */

static void test_fold_sums_into_a_wider_accumulator() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};
    int64_t acc = 0;

    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, buf, 4), &acc, sum_i32_into_i64, nullptr);

    TEST_ASSERT_EQUAL_INT64(10, acc);
}

// the elems are wide enough that a 32-bit accumulator would overflow
static void test_fold_accumulator_type_is_the_callers() {
    constexpr int32_t buf[3] = {2000000000, 2000000000, 2000000000};
    int64_t acc = 0;

    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, buf, 3), &acc, sum_i32_into_i64, nullptr);

    TEST_ASSERT_EQUAL_INT64(6000000000LL, acc);
}

// an empty span leaves the accumulator alone, so the initial value acts
// as the identity of the folded operation
static void test_fold_of_an_empty_span_keeps_the_seed() {
    int64_t sum = 42;
    int32_t product = 7;

    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), &sum, sum_i32_into_i64, nullptr);
    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), &product, product_i32, nullptr);

    TEST_ASSERT_EQUAL_INT64(42, sum);
    TEST_ASSERT_EQUAL_INT32(7, product);
}

static void test_fold_starts_from_the_seed() {
    constexpr int32_t buf[3] = {1, 2, 3};
    int64_t acc = 100;

    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, buf, 3), &acc, sum_i32_into_i64, nullptr);

    TEST_ASSERT_EQUAL_INT64(106, acc);
}

static void test_fold_walks_front_to_back() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};
    char acc[8] = "";

    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, buf, 4), acc, append_digit, nullptr);

    TEST_ASSERT_EQUAL_STRING("1234", acc);
}

static void test_fold_passes_the_ctx_through() {
    constexpr int32_t buf[5] = {1, 5, 2, 4, 3};
    int32_t bound = 2;
    size_t acc = 0;

    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, buf, 5), &acc, count_above, &bound);

    TEST_ASSERT_EQUAL_size_t(3, acc);
}

static void test_fold_sees_whole_elems() {
    constexpr Pair buf[3] = {{1, 100}, {2, 200}, {3, 300}};
    int64_t acc = 0;

    tda_span_fold(TDA_SPAN_FROM_DATA(Pair, buf, 3), &acc, sum_pair_a, nullptr);

    TEST_ASSERT_EQUAL_INT64(6, acc);
}

/* ========== fold_back ========== */

static void test_fold_back_walks_back_to_front() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};
    char acc[8] = "";

    tda_span_fold_back(TDA_SPAN_FROM_DATA(int32_t, buf, 4), acc, append_digit, nullptr);

    TEST_ASSERT_EQUAL_STRING("4321", acc);
}

// with an associative operation the direction cannot be observed...
static void test_fold_back_agrees_with_fold_when_associative() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};
    int64_t left = 0;
    int64_t right = 0;

    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, buf, 4), &left, sum_i32_into_i64, nullptr);
    tda_span_fold_back(TDA_SPAN_FROM_DATA(int32_t, buf, 4), &right, sum_i32_into_i64, nullptr);

    TEST_ASSERT_EQUAL_INT64(left, right);
}

// ...and with an order-sensitive one it decides the answer
static void test_fold_back_differs_from_fold_when_order_matters() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};
    int32_t left = 0;
    int32_t right = 0;

    tda_span_fold(TDA_SPAN_FROM_DATA(int32_t, buf, 4), &left, horner_i32, nullptr);
    tda_span_fold_back(TDA_SPAN_FROM_DATA(int32_t, buf, 4), &right, horner_i32, nullptr);

    TEST_ASSERT_EQUAL_INT32(1234, left);
    TEST_ASSERT_EQUAL_INT32(4321, right);
}

static void test_fold_back_of_an_empty_span_keeps_the_seed() {
    int64_t acc = 5;

    tda_span_fold_back(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), &acc, sum_i32_into_i64, nullptr);

    TEST_ASSERT_EQUAL_INT64(5, acc);
}

/* ========== partial_sum ========== */

static void test_partial_sum_keeps_running_totals() {
    constexpr int32_t src[5] = {1, 2, 3, 4, 5};
    int32_t dst[5] = {0};

    tda_span_partial_sum(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 5),
        TDA_SPAN_FROM_DATA(int32_t, src, 5),
        add_i32,
        nullptr
    );

    constexpr int32_t want[5] = {1, 3, 6, 10, 15};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 5);
}

// the first elem is copied, not combined: there is nothing to combine it with
static void test_partial_sum_copies_the_first_elem() {
    constexpr int32_t src[1] = {7};
    int32_t dst[1] = {0};

    tda_span_partial_sum(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 1),
        TDA_SPAN_FROM_DATA(int32_t, src, 1),
        add_i32,
        nullptr
    );

    TEST_ASSERT_EQUAL_INT32(7, dst[0]);
}

static void test_partial_sum_of_an_empty_span_writes_nothing() {
    tda_span_partial_sum(
        TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0),
        TDA_SPAN_FROM_DATA(int32_t, nullptr, 0),
        add_i32,
        nullptr
    );
}

static void test_partial_sum_passes_the_ctx_through() {
    constexpr int32_t src[5] = {1, 2, 3, 4, 5};
    int32_t dst[5] = {0};
    int32_t cap = 7;

    tda_span_partial_sum(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 5),
        TDA_SPAN_FROM_DATA(int32_t, src, 5),
        add_capped,
        &cap
    );

    constexpr int32_t want[5] = {1, 3, 6, 7, 7};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 5);
}

// each step reads what the previous step wrote, not the source
static void test_partial_sum_feeds_on_its_own_output() {
    constexpr int32_t src[4] = {1, 1, 1, 1};
    int32_t dst[4] = {0};

    tda_span_partial_sum(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 4),
        TDA_SPAN_FROM_DATA(int32_t, src, 4),
        add_i32,
        nullptr
    );

    constexpr int32_t want[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 4);
}

static void test_partial_sum_combines_whole_elems() {
    constexpr Pair src[3] = {{1, 10}, {2, 20}, {3, 30}};
    Pair dst[3] = {0};

    tda_span_partial_sum(TDA_SPAN_FROM_DATA_MUT(Pair, dst, 3), TDA_SPAN_FROM_DATA(Pair, src, 3), add_pairs, nullptr);

    TEST_ASSERT_EQUAL_INT64(6, dst[2].a);
    TEST_ASSERT_EQUAL_INT64(60, dst[2].b);
}

/* ========== adjacent_difference ========== */

static void test_adjacent_difference_reports_the_steps() {
    constexpr int32_t src[5] = {1, 3, 6, 10, 15};
    int32_t dst[5] = {0};

    tda_span_adjacent_difference(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 5),
        TDA_SPAN_FROM_DATA(int32_t, src, 5),
        sub_i32,
        nullptr
    );

    constexpr int32_t want[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 5);
}

// it reads the source on both sides, so its own output never feeds back
static void test_adjacent_difference_reads_only_the_source() {
    constexpr int32_t src[4] = {5, 5, 5, 5};
    int32_t dst[4] = {9, 9, 9, 9};

    tda_span_adjacent_difference(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 4),
        TDA_SPAN_FROM_DATA(int32_t, src, 4),
        sub_i32,
        nullptr
    );

    constexpr int32_t want[4] = {5, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, 4);
}

static void test_adjacent_difference_of_short_spans() {
    constexpr int32_t src[1] = {7};
    int32_t dst[1] = {0};

    tda_span_adjacent_difference(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 1),
        TDA_SPAN_FROM_DATA(int32_t, src, 1),
        sub_i32,
        nullptr
    );
    TEST_ASSERT_EQUAL_INT32(7, dst[0]);

    tda_span_adjacent_difference(
        TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0),
        TDA_SPAN_FROM_DATA(int32_t, nullptr, 0),
        sub_i32,
        nullptr
    );
}

// the two are inverses: differencing a scan gives the original back
static void test_adjacent_difference_undoes_partial_sum() {
    constexpr int32_t src[6] = {4, -2, 7, 0, 1, 9};
    int32_t scanned[6] = {0};
    int32_t back[6] = {0};

    tda_span_partial_sum(
        TDA_SPAN_FROM_DATA_MUT(int32_t, scanned, 6),
        TDA_SPAN_FROM_DATA(int32_t, src, 6),
        add_i32,
        nullptr
    );
    tda_span_adjacent_difference(
        TDA_SPAN_FROM_DATA_MUT(int32_t, back, 6),
        TDA_SPAN_FROM_DATA(int32_t, scanned, 6),
        sub_i32,
        nullptr
    );

    TEST_ASSERT_EQUAL_INT32_ARRAY(src, back, 6);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_fold_sums_into_a_wider_accumulator);
    RUN_TEST(test_fold_accumulator_type_is_the_callers);
    RUN_TEST(test_fold_of_an_empty_span_keeps_the_seed);
    RUN_TEST(test_fold_starts_from_the_seed);
    RUN_TEST(test_fold_walks_front_to_back);
    RUN_TEST(test_fold_passes_the_ctx_through);
    RUN_TEST(test_fold_sees_whole_elems);

    RUN_TEST(test_fold_back_walks_back_to_front);
    RUN_TEST(test_fold_back_agrees_with_fold_when_associative);
    RUN_TEST(test_fold_back_differs_from_fold_when_order_matters);
    RUN_TEST(test_fold_back_of_an_empty_span_keeps_the_seed);

    RUN_TEST(test_partial_sum_keeps_running_totals);
    RUN_TEST(test_partial_sum_copies_the_first_elem);
    RUN_TEST(test_partial_sum_of_an_empty_span_writes_nothing);
    RUN_TEST(test_partial_sum_passes_the_ctx_through);
    RUN_TEST(test_partial_sum_feeds_on_its_own_output);
    RUN_TEST(test_partial_sum_combines_whole_elems);

    RUN_TEST(test_adjacent_difference_reports_the_steps);
    RUN_TEST(test_adjacent_difference_reads_only_the_source);
    RUN_TEST(test_adjacent_difference_of_short_spans);
    RUN_TEST(test_adjacent_difference_undoes_partial_sum);

    return UNITY_END();
}
