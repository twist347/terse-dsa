#include "tda/algo/set.h"
#include "tda/algo/sort.h"
#include "tda/core/cmp.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

/* ========== helpers ========== */

// a value that cannot come out of any operation, so anything left over in dst is visible
static constexpr int32_t UNTOUCHED = 0x7f7f7f7f;

static tda_Span span_of(const int32_t *v, size_t n) {
    return tda_span_from_data(v, n, TDA_LAYOUT_OF(int32_t));
}

// runs 'op' into a dst prefilled with UNTOUCHED and checks the elems, the returned
// count, and that nothing was written past that count
static void assert_writes(
    size_t (*op)(tda_SpanMut, tda_Span, tda_Span, tda_Cmp, void *),
    const int32_t *a, size_t an,
    const int32_t *b, size_t bn,
    const int32_t *want, size_t wn,
    size_t cap
) {
    int32_t dst[32];
    TEST_ASSERT_TRUE(cap <= 32);

    for (size_t i = 0; i < 32; ++i) {
        dst[i] = UNTOUCHED;
    }

    const size_t got = op(
        tda_span_from_data_mut(dst, cap, TDA_LAYOUT_OF(int32_t)),
        span_of(a, an),
        span_of(b, bn),
        tda_cmp_i32,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(wn, got);

    // Unity refuses to compare zero-length arrays, and these ops often give an empty
    // result; the count above is then the whole assertion
    if (wn > 0) {
        TEST_ASSERT_EQUAL_INT32_ARRAY(want, dst, wn);
    }

    for (size_t i = got; i < 32; ++i) {
        TEST_ASSERT_EQUAL_INT32_MESSAGE(UNTOUCHED, dst[i], "wrote past the count it reported");
    }

    // the result is sorted, which lets these compose without a sort in between
    TEST_ASSERT_TRUE(tda_span_is_sorted(span_of(dst, got), tda_cmp_i32, nullptr));
}

/* ========== union ========== */

static void test_union_keeps_the_greater_count_of_equal_elems() {
    constexpr int32_t a[] = {1, 2, 2, 2, 5};
    constexpr int32_t b[] = {2, 2, 3};
    constexpr int32_t want[] = {1, 2, 2, 2, 3, 5};

    assert_writes(tda_span_set_union, a, 5, b, 3, want, 6, 8);
}

static void test_union_of_disjoint_spans_is_everything() {
    constexpr int32_t a[] = {1, 3, 5};
    constexpr int32_t b[] = {2, 4, 6};
    constexpr int32_t want[] = {1, 2, 3, 4, 5, 6};

    assert_writes(tda_span_set_union, a, 3, b, 3, want, 6, 6);
}

static void test_union_with_an_empty_span_is_the_other_one() {
    constexpr int32_t a[] = {1, 2, 3};
    constexpr int32_t want[] = {1, 2, 3};

    assert_writes(tda_span_set_union, a, 3, nullptr, 0, want, 3, 3);
    assert_writes(tda_span_set_union, nullptr, 0, a, 3, want, 3, 3);
}

static void test_union_of_two_empty_spans_writes_nothing() {
    assert_writes(tda_span_set_union, nullptr, 0, nullptr, 0, nullptr, 0, 0);
}

/* ========== intersection ========== */

static void test_intersection_takes_the_lesser_count() {
    constexpr int32_t a[] = {1, 2, 2, 2, 5};
    constexpr int32_t b[] = {2, 2, 3};
    constexpr int32_t want[] = {2, 2};

    assert_writes(tda_span_set_intersection, a, 5, b, 3, want, 2, 3);
}

static void test_intersection_of_disjoint_spans_is_empty() {
    constexpr int32_t a[] = {1, 3, 5};
    constexpr int32_t b[] = {2, 4, 6};

    assert_writes(tda_span_set_intersection, a, 3, b, 3, nullptr, 0, 3);
}

static void test_intersection_with_an_empty_span_is_empty() {
    constexpr int32_t a[] = {1, 2, 3};

    assert_writes(tda_span_set_intersection, a, 3, nullptr, 0, nullptr, 0, 0);
    assert_writes(tda_span_set_intersection, nullptr, 0, a, 3, nullptr, 0, 0);
}

// Equal by 'cmp' is not identical, so which side an elem comes from is observable and
// deliberate. On plain int32_t both choices look the same, so the payload is the
// witness, as for equal keys in a heap
static void test_intersection_takes_its_elems_from_the_first_span() {
    constexpr Pair a[] = {{.a = 1, .b = 10}, {.a = 2, .b = 20}};
    constexpr Pair b[] = {{.a = 1, .b = 99}, {.a = 2, .b = 98}};
    Pair dst[2] = {};

    const size_t got = tda_span_set_intersection(
        tda_span_from_data_mut(dst, 2, TDA_LAYOUT_OF(Pair)),
        tda_span_from_data(a, 2, TDA_LAYOUT_OF(Pair)),
        tda_span_from_data(b, 2, TDA_LAYOUT_OF(Pair)),
        tda_test_pair_cmp_a,
        nullptr
    );

    TEST_ASSERT_EQUAL_size_t(2, got);
    TEST_ASSERT_EQUAL_INT64(10, dst[0].b);
    TEST_ASSERT_EQUAL_INT64(20, dst[1].b);
}

/* ========== difference ========== */

static void test_difference_spends_one_copy_per_copy() {
    constexpr int32_t a[] = {1, 2, 2, 2, 5};
    constexpr int32_t b[] = {2, 2, 3};
    constexpr int32_t want[] = {1, 2, 5};

    assert_writes(tda_span_set_difference, a, 5, b, 3, want, 3, 5);
}

// elems only 'b' has cancel nothing: the result is never longer than 'a', and what is
// left over in 'b' must not shorten it either
static void test_difference_ignores_what_only_the_second_span_has() {
    constexpr int32_t a[] = {1, 2};
    constexpr int32_t b[] = {3, 4, 5, 6};
    constexpr int32_t want[] = {1, 2};

    assert_writes(tda_span_set_difference, a, 2, b, 4, want, 2, 2);
}

static void test_difference_from_an_equal_span_is_empty() {
    constexpr int32_t a[] = {1, 2, 3};

    assert_writes(tda_span_set_difference, a, 3, a, 3, nullptr, 0, 3);
}

static void test_difference_with_an_empty_second_span_is_the_first() {
    constexpr int32_t a[] = {1, 2, 3};
    constexpr int32_t want[] = {1, 2, 3};

    assert_writes(tda_span_set_difference, a, 3, nullptr, 0, want, 3, 3);
}

/* ========== symmetric difference ========== */

static void test_symmetric_difference_keeps_the_surplus() {
    constexpr int32_t a[] = {1, 2, 2, 2, 5};
    constexpr int32_t b[] = {2, 2, 3};
    constexpr int32_t want[] = {1, 2, 3, 5};

    assert_writes(tda_span_set_symmetric_difference, a, 5, b, 3, want, 4, 8);
}

// the surplus is taken from whichever side has more, so it is not always 'a'
static void test_symmetric_difference_takes_the_surplus_from_either_side() {
    constexpr int32_t a[] = {7};
    constexpr int32_t b[] = {7, 7, 7};
    constexpr int32_t want[] = {7, 7};

    assert_writes(tda_span_set_symmetric_difference, a, 1, b, 3, want, 2, 4);
}

static void test_symmetric_difference_of_equal_spans_is_empty() {
    constexpr int32_t a[] = {1, 2, 3};

    assert_writes(tda_span_set_symmetric_difference, a, 3, a, 3, nullptr, 0, 6);
}

static void test_symmetric_difference_of_disjoint_spans_is_everything() {
    constexpr int32_t a[] = {1, 3};
    constexpr int32_t b[] = {2, 4};
    constexpr int32_t want[] = {1, 2, 3, 4};

    assert_writes(tda_span_set_symmetric_difference, a, 2, b, 2, want, 4, 4);
}

/* ========== includes ========== */

static void test_includes_accepts_a_subset() {
    constexpr int32_t sup[] = {1, 2, 3, 4, 5};
    constexpr int32_t sub[] = {2, 4};

    TEST_ASSERT_TRUE(tda_span_includes(span_of(sup, 5), span_of(sub, 2), tda_cmp_i32, nullptr));
}

static void test_includes_rejects_a_missing_elem() {
    constexpr int32_t sup[] = {1, 2, 3};
    constexpr int32_t sub[] = {2, 9};

    TEST_ASSERT_FALSE(tda_span_includes(span_of(sup, 3), span_of(sub, 2), tda_cmp_i32, nullptr));
}

// a duplicate needs a duplicate: one copy cannot account for two
static void test_includes_counts_duplicates() {
    constexpr int32_t sup[] = {1, 2, 2, 3};
    constexpr int32_t two[] = {2, 2};
    constexpr int32_t three[] = {2, 2, 2};

    TEST_ASSERT_TRUE(tda_span_includes(span_of(sup, 4), span_of(two, 2), tda_cmp_i32, nullptr));
    TEST_ASSERT_FALSE(tda_span_includes(span_of(sup, 4), span_of(three, 3), tda_cmp_i32, nullptr));
}

static void test_includes_of_an_empty_sub_is_true() {
    constexpr int32_t sup[] = {1, 2, 3};

    TEST_ASSERT_TRUE(tda_span_includes(span_of(sup, 3), span_of(nullptr, 0), tda_cmp_i32, nullptr));
    TEST_ASSERT_TRUE(tda_span_includes(span_of(nullptr, 0), span_of(nullptr, 0), tda_cmp_i32, nullptr));
}

static void test_includes_rejects_anything_from_an_empty_sup() {
    constexpr int32_t sub[] = {1};

    TEST_ASSERT_FALSE(tda_span_includes(span_of(nullptr, 0), span_of(sub, 1), tda_cmp_i32, nullptr));
}

// the last elem of 'sub' has to be reached without walking off 'sup'
static void test_includes_rejects_a_sub_that_runs_past_the_end() {
    constexpr int32_t sup[] = {1, 2};
    constexpr int32_t sub[] = {1, 2, 3};

    TEST_ASSERT_FALSE(tda_span_includes(span_of(sup, 2), span_of(sub, 3), tda_cmp_i32, nullptr));
}

/* ========== exhaustive ========== */

/*
 * Each op against a reference that shares no code with it: multisets are counted per
 * value, and the answer is built from the counts by the rule in set.h. The sweep visits
 * every pair of sorted multisets over a four-symbol alphabet up to length four, on both
 * sides: 24500 checks in all.
 *
 * Small on purpose: a two-pointer walk fails on long inputs only where it already fails
 * on short ones, and every arrangement of a short one is reachable.
 */

#define ALPHA 4
#define MAXLEN 4

static void count_values(const int32_t *v, size_t n, int *out) {
    memset(out, 0, ALPHA * sizeof(int));
    for (size_t i = 0; i < n; ++i) {
        ++out[v[i]];
    }
}

static size_t expand_counts(const int *c, int32_t *out) {
    size_t n = 0;
    for (int32_t v = 0; v < ALPHA; ++v) {
        for (int k = 0; k < c[v]; ++k) {
            out[n++] = v;
        }
    }

    return n;
}

static void check_pair(const int32_t *a, size_t an, const int32_t *b, size_t bn) {
    int ca[ALPHA], cb[ALPHA], want_counts[ALPHA];
    int32_t want[2 * MAXLEN];

    count_values(a, an, ca);
    count_values(b, bn, cb);

    static size_t (*const ops[4])(tda_SpanMut, tda_Span, tda_Span, tda_Cmp, void *) = {
        tda_span_set_union,
        tda_span_set_intersection,
        tda_span_set_difference,
        tda_span_set_symmetric_difference,
    };

    for (size_t k = 0; k < 4; ++k) {
        for (int v = 0; v < ALPHA; ++v) {
            const int m = ca[v], n = cb[v];
            switch (k) {
                case 0: want_counts[v] = m > n ? m : n;
                    break;
                case 1: want_counts[v] = m < n ? m : n;
                    break;
                case 2: want_counts[v] = m > n ? m - n : 0;
                    break;
                default: want_counts[v] = m > n ? m - n : n - m;
                    break;
            }
        }

        const size_t wn = expand_counts(want_counts, want);
        const size_t cap = k == 1 ? (an < bn ? an : bn) : (k == 2 ? an : an + bn);

        assert_writes(ops[k], a, an, b, bn, want, wn, cap);
    }

    bool want_includes = true;
    for (int v = 0; v < ALPHA; ++v) {
        if (cb[v] > ca[v]) {
            want_includes = false;
        }
    }
    TEST_ASSERT_EQUAL_INT(want_includes, tda_span_includes(span_of(a, an), span_of(b, bn), tda_cmp_i32, nullptr));
}

static int32_t sweep_a[MAXLEN];
static int32_t sweep_b[MAXLEN];

static void sweep_b_of_len(size_t n, size_t i, int32_t lo, size_t an) {
    if (i == n) {
        check_pair(sweep_a, an, sweep_b, n);
        return;
    }
    for (int32_t v = lo; v < ALPHA; ++v) {
        sweep_b[i] = v;
        sweep_b_of_len(n, i + 1, v, an);
    }
}

static void sweep_a_of_len(size_t n, size_t i, int32_t lo) {
    if (i == n) {
        for (size_t bn = 0; bn <= MAXLEN; ++bn) {
            sweep_b_of_len(bn, 0, 0, n);
        }
        return;
    }
    for (int32_t v = lo; v < ALPHA; ++v) {
        sweep_a[i] = v;
        sweep_a_of_len(n, i + 1, v);
    }
}

static void test_every_op_agrees_with_a_counting_reference() {
    for (size_t an = 0; an <= MAXLEN; ++an) {
        sweep_a_of_len(an, 0, 0);
    }
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_union_keeps_the_greater_count_of_equal_elems);
    RUN_TEST(test_union_of_disjoint_spans_is_everything);
    RUN_TEST(test_union_with_an_empty_span_is_the_other_one);
    RUN_TEST(test_union_of_two_empty_spans_writes_nothing);

    RUN_TEST(test_intersection_takes_the_lesser_count);
    RUN_TEST(test_intersection_of_disjoint_spans_is_empty);
    RUN_TEST(test_intersection_with_an_empty_span_is_empty);
    RUN_TEST(test_intersection_takes_its_elems_from_the_first_span);

    RUN_TEST(test_difference_spends_one_copy_per_copy);
    RUN_TEST(test_difference_ignores_what_only_the_second_span_has);
    RUN_TEST(test_difference_from_an_equal_span_is_empty);
    RUN_TEST(test_difference_with_an_empty_second_span_is_the_first);

    RUN_TEST(test_symmetric_difference_keeps_the_surplus);
    RUN_TEST(test_symmetric_difference_takes_the_surplus_from_either_side);
    RUN_TEST(test_symmetric_difference_of_equal_spans_is_empty);
    RUN_TEST(test_symmetric_difference_of_disjoint_spans_is_everything);

    RUN_TEST(test_includes_accepts_a_subset);
    RUN_TEST(test_includes_rejects_a_missing_elem);
    RUN_TEST(test_includes_counts_duplicates);
    RUN_TEST(test_includes_of_an_empty_sub_is_true);
    RUN_TEST(test_includes_rejects_anything_from_an_empty_sup);
    RUN_TEST(test_includes_rejects_a_sub_that_runs_past_the_end);

    RUN_TEST(test_every_op_agrees_with_a_counting_reference);

    return UNITY_END();
}
