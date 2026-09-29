#include "tda/algo/merge.h"
#include "tda/algo/sort.h"
#include "tda/alloc/default.h"

#include "support/probe.h"

#include <unity.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

// ordered by key only, so tag can witness which side an equal element came from
typedef struct {
    int32_t key;
    int32_t tag;
} Tagged;

static int cmp_tagged(const void *a, const void *b) {
    return tda_cmp_i32(&((const Tagged *) a)->key, &((const Tagged *) b)->key);
}

/* ========== merge ========== */

static void test_merge_interleaves_both_sides() {
    constexpr int32_t a[3] = {1, 3, 5};
    constexpr int32_t b[3] = {2, 4, 6};
    int32_t dst[6] = {0};

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 6),
        TDA_SPAN_FROM_DATA(int32_t, a, 3),
        TDA_SPAN_FROM_DATA(int32_t, b, 3),
        tda_cmp_i32
    );

    constexpr int32_t expected[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, dst, 6);
}

// no interleaving at all — the whole tail must still be drained
static void test_merge_disjoint_ranges() {
    constexpr int32_t a[3] = {1, 2, 3};
    constexpr int32_t b[3] = {7, 8, 9};
    int32_t dst[6] = {0};

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 6),
        TDA_SPAN_FROM_DATA(int32_t, a, 3),
        TDA_SPAN_FROM_DATA(int32_t, b, 3),
        tda_cmp_i32
    );

    constexpr int32_t expected[6] = {1, 2, 3, 7, 8, 9};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, dst, 6);
}

static void test_merge_second_side_comes_first() {
    constexpr int32_t a[2] = {7, 8};
    constexpr int32_t b[2] = {1, 2};
    int32_t dst[4] = {0};

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 4),
        TDA_SPAN_FROM_DATA(int32_t, a, 2),
        TDA_SPAN_FROM_DATA(int32_t, b, 2),
        tda_cmp_i32
    );

    constexpr int32_t expected[4] = {1, 2, 7, 8};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, dst, 4);
}

static void test_merge_uneven_lengths() {
    constexpr int32_t a[1] = {4};
    constexpr int32_t b[5] = {1, 2, 3, 5, 6};
    int32_t dst[6] = {0};

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 6),
        TDA_SPAN_FROM_DATA(int32_t, a, 1),
        TDA_SPAN_FROM_DATA(int32_t, b, 5),
        tda_cmp_i32
    );

    constexpr int32_t expected[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, dst, 6);
}

static void test_merge_with_an_empty_side() {
    constexpr int32_t a[3] = {1, 2, 3};
    int32_t dst[3] = {0};

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 3),
        TDA_SPAN_FROM_DATA(int32_t, a, 3),
        TDA_SPAN_FROM_DATA(int32_t, a, 0),
        tda_cmp_i32
    );
    TEST_ASSERT_EQUAL_INT32_ARRAY(a, dst, 3);

    // and the mirror case — the empty side leading
    int32_t dst2[3] = {0};
    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst2, 3),
        TDA_SPAN_FROM_DATA(int32_t, a, 0),
        TDA_SPAN_FROM_DATA(int32_t, a, 3),
        tda_cmp_i32
    );
    TEST_ASSERT_EQUAL_INT32_ARRAY(a, dst2, 3);
}

static void test_merge_both_sides_empty() {
    constexpr int32_t a[1] = {1};
    int32_t dst[1] = {42};

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 0),
        TDA_SPAN_FROM_DATA(int32_t, a, 0),
        TDA_SPAN_FROM_DATA(int32_t, a, 0),
        tda_cmp_i32
    );

    TEST_ASSERT_EQUAL_INT32(42, dst[0]);
}

static void test_merge_keeps_duplicates() {
    constexpr int32_t a[3] = {1, 2, 2};
    constexpr int32_t b[2] = {2, 3};
    int32_t dst[5] = {0};

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 5),
        TDA_SPAN_FROM_DATA(int32_t, a, 3),
        TDA_SPAN_FROM_DATA(int32_t, b, 2),
        tda_cmp_i32
    );

    constexpr int32_t expected[5] = {1, 2, 2, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, dst, 5);
}

// stability: on a tie the element from the first span must be emitted first
static void test_merge_is_stable_on_ties() {
    constexpr Tagged a[2] = {{1, 100}, {2, 101}};
    constexpr Tagged b[2] = {{1, 200}, {2, 201}};
    Tagged dst[4] = {};

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(Tagged, dst, 4),
        TDA_SPAN_FROM_DATA(Tagged, a, 2),
        TDA_SPAN_FROM_DATA(Tagged, b, 2),
        cmp_tagged
    );

    TEST_ASSERT_EQUAL_INT32(1, dst[0].key);
    TEST_ASSERT_EQUAL_INT32(100, dst[0].tag);
    TEST_ASSERT_EQUAL_INT32(1, dst[1].key);
    TEST_ASSERT_EQUAL_INT32(200, dst[1].tag);
    TEST_ASSERT_EQUAL_INT32(2, dst[2].key);
    TEST_ASSERT_EQUAL_INT32(101, dst[2].tag);
    TEST_ASSERT_EQUAL_INT32(2, dst[3].key);
    TEST_ASSERT_EQUAL_INT32(201, dst[3].tag);
}

static void test_merge_writes_only_into_the_destination_view() {
    constexpr int32_t a[1] = {1};
    constexpr int32_t b[1] = {2};
    int32_t dst[4] = {9, 0, 0, 9};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, dst, 4);

    tda_span_merge(
        tda_span_sub_mut(s, 1, 2),
        TDA_SPAN_FROM_DATA(int32_t, a, 1),
        TDA_SPAN_FROM_DATA(int32_t, b, 1),
        tda_cmp_i32
    );

    constexpr int32_t expected[4] = {9, 1, 2, 9};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, dst, 4);
}

/* ========== inplace merge ========== */

// the oracle is the libc qsort over a copy: a merge of two sorted runs is exactly the
// sort of their concatenation, and qsort shares no code with what is tested
static void assert_merges_in_place(const int32_t *src, size_t len, size_t mid, tda_Al *al) {
    int32_t buf[16];
    int32_t want[16];
    TEST_ASSERT_TRUE(len <= 16);

    memcpy(buf, src, len * sizeof(int32_t));
    memcpy(want, src, len * sizeof(int32_t));
    qsort(want, len, sizeof(int32_t), tda_cmp_i32);

    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, len), mid, tda_cmp_i32, al);

    if (len > 0) {
        TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, len);
    }
}

// there are two implementations behind one name, and a case that exercises only one of
// them tests half the function
static void assert_merges_both_ways(const int32_t *src, size_t len, size_t mid) {
    assert_merges_in_place(src, len, mid, tda_al_default());
    assert_merges_in_place(src, len, mid, nullptr);
}

static void test_inplace_merge_interleaves_both_runs() {
    constexpr int32_t src[6] = {1, 3, 5, 2, 4, 6};

    assert_merges_both_ways(src, 6, 3);
}

static void test_inplace_merge_of_disjoint_runs_leaves_them_alone() {
    constexpr int32_t src[6] = {1, 2, 3, 4, 5, 6};

    assert_merges_both_ways(src, 6, 3);
}

// the whole second run comes first, so the merge degenerates into a rotation
static void test_inplace_merge_when_the_second_run_comes_first() {
    constexpr int32_t src[6] = {4, 5, 6, 1, 2, 3};

    assert_merges_both_ways(src, 6, 3);
}

static void test_inplace_merge_uneven_runs() {
    constexpr int32_t src[7] = {5, 1, 2, 3, 4, 6, 7};

    assert_merges_both_ways(src, 7, 1);
    assert_merges_both_ways((const int32_t[]){1, 2, 3, 4, 6, 7, 5}, 7, 6);
}

static void test_inplace_merge_keeps_duplicates() {
    constexpr int32_t src[8] = {1, 2, 2, 3, 2, 2, 3, 9};

    assert_merges_both_ways(src, 8, 4);
}

static void test_inplace_merge_at_the_edges_changes_nothing() {
    constexpr int32_t src[4] = {2, 4, 1, 3};
    int32_t buf[4];

    memcpy(buf, src, sizeof(buf));
    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), 0, tda_cmp_i32, tda_al_default());
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, buf, 4);

    memcpy(buf, src, sizeof(buf));
    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), 4, tda_cmp_i32, tda_al_default());
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, buf, 4);
}

static void test_inplace_merge_of_short_spans() {
    int32_t one[1] = {7};
    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, one, 1), 0, tda_cmp_i32, tda_al_default());
    TEST_ASSERT_EQUAL_INT32(7, one[0]);

    assert_merges_both_ways((const int32_t[]){2, 1}, 2, 1);
    assert_merges_both_ways((const int32_t[]){1, 2}, 2, 1);
}

// stability: equal keys keep the run they came from, and the left run comes first.
// On plain int32_t this is invisible — equal elems are indistinguishable — so the tag is
// the witness, the same way it is for tda_span_merge above
static void test_inplace_merge_is_stable_on_ties() {
    Tagged buf[6] = {
        {1, 100}, {2, 101}, {2, 102},
        {1, 200}, {2, 201}, {3, 202},
    };

    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(Tagged, buf, 6), 3, cmp_tagged, tda_al_default());

    constexpr int32_t want_keys[6] = {1, 1, 2, 2, 2, 3};
    constexpr int32_t want_tags[6] = {100, 200, 101, 102, 201, 202};

    for (size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_INT32(want_keys[i], buf[i].key);
        TEST_ASSERT_EQUAL_INT32(want_tags[i], buf[i].tag);
    }
}

/*
 * Every pair of sorted runs over a three-symbol alphabet, both runs up to length four, at
 * every split — 5625 spans in all. Small on purpose: the recursion cuts the longer run in
 * half and searches the other, so anything it can get wrong on a long input it can already
 * get wrong on a short one, and here every arrangement is reachable.
 *
 * Every elem carries its starting position as a tag, which turns stability into something
 * checkable on every one of those spans: equal keys must come out with rising tags,
 * because the left run's elems all started before the right run's. A single hand-written
 * stable case does NOT pin this down — with three equal keys against three there is only
 * one branch of the recursion to walk, and swapping lower_bound for upper_bound produces
 * the very same answer on it.
 */
#define ALPHA 3
#define RUNLEN 4

static int32_t sweep_keys[2 * RUNLEN];

static void check_sweep(size_t left_len, size_t right_len, tda_Al *al) {
    const size_t len = left_len + right_len;

    Tagged buf[2 * RUNLEN];
    int32_t want_keys[2 * RUNLEN];

    for (size_t i = 0; i < len; ++i) {
        buf[i] = (Tagged){.key = sweep_keys[i], .tag = (int32_t) i};
        want_keys[i] = sweep_keys[i];
    }
    qsort(want_keys, len, sizeof(int32_t), tda_cmp_i32);

    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(Tagged, buf, len), left_len, cmp_tagged, al);

    for (size_t i = 0; i < len; ++i) {
        TEST_ASSERT_EQUAL_INT32_MESSAGE(want_keys[i], buf[i].key, "not the sorted order");

        // rising tags within a run of equal keys IS stability: the left run's elems
        // started at the lower positions
        if (i > 0 && buf[i - 1].key == buf[i].key) {
            TEST_ASSERT_TRUE_MESSAGE(buf[i - 1].tag < buf[i].tag, "equal keys changed places");
        }
    }
}

static tda_Al *sweep_al;

static void sweep_right(size_t n, size_t i, int32_t lo, size_t left_len) {
    if (i == n) {
        check_sweep(left_len, n, sweep_al);
        return;
    }
    for (int32_t v = lo; v < ALPHA; ++v) {
        sweep_keys[left_len + i] = v;
        sweep_right(n, i + 1, v, left_len);
    }
}

static void sweep_left(size_t n, size_t i, int32_t lo) {
    if (i == n) {
        for (size_t right_len = 0; right_len <= RUNLEN; ++right_len) {
            sweep_right(right_len, 0, 0, n);
        }
        return;
    }
    for (int32_t v = lo; v < ALPHA; ++v) {
        sweep_keys[i] = v;
        sweep_left(n, i + 1, v);
    }
}

static void test_inplace_merge_sorts_and_stays_stable_on_every_pair_of_runs() {
    sweep_al = tda_al_default();
    for (size_t left_len = 0; left_len <= RUNLEN; ++left_len) {
        sweep_left(left_len, 0, 0);
    }
}

// the same sweep down the other path: both must give the same answer, and both must be
// stable. Without this the fallback would be covered by a handful of cases at most
static void test_inplace_merge_without_an_allocator_agrees_on_every_pair_of_runs() {
    sweep_al = nullptr;
    for (size_t left_len = 0; left_len <= RUNLEN; ++left_len) {
        sweep_left(left_len, 0, 0);
    }
}

/* ========== inplace merge: the buffer ========== */

// the point of the buffered path is that it parks the SHORTER run, so the buffer is never
// more than half the span. A version that copied the left run whatever its length would
// pass every test above and still ask for twice the memory on a lopsided split
static void test_inplace_merge_asks_only_for_the_shorter_run() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    int32_t buf[8] = {5, 1, 2, 3, 4, 6, 7, 8};

    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 8), 1, tda_cmp_i32, &al);

    TEST_ASSERT_EQUAL_size_t(1, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_size_t(1 * sizeof(int32_t), probe.last_alloc_size);

    tda_test_probe_reset(&probe);
    int32_t other[8] = {1, 2, 3, 4, 5, 6, 7, 0};

    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, other, 8), 7, tda_cmp_i32, &al);

    TEST_ASSERT_EQUAL_size_t(1 * sizeof(int32_t), probe.last_alloc_size);
}

static void test_inplace_merge_gives_the_buffer_back() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    int32_t buf[6] = {1, 3, 5, 2, 4, 6};

    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), 3, tda_cmp_i32, &al);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a refused buffer is not an error here, only the slower way to the same answer
static void test_inplace_merge_falls_back_when_the_buffer_is_refused() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);
    tda_test_probe_fail_after_next(&probe, 0);

    int32_t buf[6] = {1, 3, 5, 2, 4, 6};

    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), 3, tda_cmp_i32, &al);

    constexpr int32_t want[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 6);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// an empty run on either side is answered without touching the allocator at all
static void test_inplace_merge_at_the_edges_asks_for_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    int32_t buf[4] = {2, 4, 1, 3};

    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), 0, tda_cmp_i32, &al);
    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), 4, tda_cmp_i32, &al);

    TEST_ASSERT_EQUAL_size_t(0, tda_test_probe_requests(&probe));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_merge_interleaves_both_sides);
    RUN_TEST(test_merge_disjoint_ranges);
    RUN_TEST(test_merge_second_side_comes_first);
    RUN_TEST(test_merge_uneven_lengths);

    RUN_TEST(test_merge_with_an_empty_side);
    RUN_TEST(test_merge_both_sides_empty);

    RUN_TEST(test_merge_keeps_duplicates);
    RUN_TEST(test_merge_is_stable_on_ties);
    RUN_TEST(test_merge_writes_only_into_the_destination_view);

    RUN_TEST(test_inplace_merge_interleaves_both_runs);
    RUN_TEST(test_inplace_merge_of_disjoint_runs_leaves_them_alone);
    RUN_TEST(test_inplace_merge_when_the_second_run_comes_first);
    RUN_TEST(test_inplace_merge_uneven_runs);
    RUN_TEST(test_inplace_merge_keeps_duplicates);
    RUN_TEST(test_inplace_merge_at_the_edges_changes_nothing);
    RUN_TEST(test_inplace_merge_of_short_spans);
    RUN_TEST(test_inplace_merge_is_stable_on_ties);
    RUN_TEST(test_inplace_merge_sorts_and_stays_stable_on_every_pair_of_runs);
    RUN_TEST(test_inplace_merge_without_an_allocator_agrees_on_every_pair_of_runs);

    RUN_TEST(test_inplace_merge_asks_only_for_the_shorter_run);
    RUN_TEST(test_inplace_merge_gives_the_buffer_back);
    RUN_TEST(test_inplace_merge_falls_back_when_the_buffer_is_refused);
    RUN_TEST(test_inplace_merge_at_the_edges_asks_for_nothing);

    return UNITY_END();
}
