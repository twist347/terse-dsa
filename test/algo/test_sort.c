#include "tda/algo/sort.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"

#include <unity.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

// ordered by key only, so tag can witness whether equal elements kept their order
typedef struct {
    int32_t key;
    int32_t tag;
} Tagged;

static int cmp_tagged(const void *a, const void *b) {
    return tda_cmp_i32(&((const Tagged *) a)->key, &((const Tagged *) b)->key);
}

// sorts a copy with the libc qsort — an oracle that shares no code with what is tested
static void sorted_copy(int32_t *dst, const int32_t *src, size_t n) {
    memcpy(dst, src, n * sizeof(int32_t));
    qsort(dst, n, sizeof(int32_t), tda_cmp_i32);
}

// true when both hold the same elems, order aside — a reordering algorithm must never
// invent, drop or duplicate one
static bool same_elems(const int32_t *a, const int32_t *b, size_t n) {
    int32_t x[64];
    int32_t y[64];
    TEST_ASSERT_TRUE(n <= 64);

    sorted_copy(x, a, n);
    sorted_copy(y, b, n);

    return memcmp(x, y, n * sizeof(int32_t)) == 0;
}

/* ========== insertion_sort ========== */

static void test_insertion_sort_orders_a_shuffled_span() {
    int32_t buf[6] = {5, 3, 1, 4, 2, 6};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6);

    tda_span_insertion_sort(s, tda_cmp_i32);

    constexpr int32_t expected[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 6);
}

static void test_insertion_sort_already_sorted_is_unchanged() {
    int32_t buf[4] = {1, 2, 3, 4};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);

    tda_span_insertion_sort(s, tda_cmp_i32);

    constexpr int32_t expected[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 4);
}

// the worst case for insertion sort — every element travels the whole way
static void test_insertion_sort_reversed_span() {
    int32_t buf[5] = {5, 4, 3, 2, 1};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    tda_span_insertion_sort(s, tda_cmp_i32);

    constexpr int32_t expected[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 5);
}

static void test_insertion_sort_keeps_duplicates() {
    int32_t buf[6] = {3, 1, 3, 2, 1, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6);

    tda_span_insertion_sort(s, tda_cmp_i32);

    constexpr int32_t expected[6] = {1, 1, 2, 3, 3, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 6);
}

static void test_insertion_sort_empty_and_single_are_noop() {
    int32_t buf[1] = {42};

    tda_span_insertion_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 0), tda_cmp_i32);
    TEST_ASSERT_EQUAL_INT32(42, buf[0]);

    tda_span_insertion_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 1), tda_cmp_i32);
    TEST_ASSERT_EQUAL_INT32(42, buf[0]);
}

// the comparator defines the order — the algorithm must not assume ascending
static void test_insertion_sort_follows_the_comparator() {
    int32_t buf[5] = {2, 5, 1, 4, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    tda_span_insertion_sort(s, tda_cmp_desc_i32);

    constexpr int32_t expected[5] = {5, 4, 3, 2, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 5);
}

// stability: equal keys must come out in their original relative order
static void test_insertion_sort_is_stable() {
    Tagged buf[5] = {
        {2, 0}, {1, 0}, {2, 1}, {1, 1}, {2, 2},
    };
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(Tagged, buf, 5);

    tda_span_insertion_sort(s, cmp_tagged);

    TEST_ASSERT_EQUAL_INT32(1, buf[0].key);
    TEST_ASSERT_EQUAL_INT32(0, buf[0].tag);
    TEST_ASSERT_EQUAL_INT32(1, buf[1].key);
    TEST_ASSERT_EQUAL_INT32(1, buf[1].tag);
    TEST_ASSERT_EQUAL_INT32(2, buf[2].key);
    TEST_ASSERT_EQUAL_INT32(0, buf[2].tag);
    TEST_ASSERT_EQUAL_INT32(2, buf[3].key);
    TEST_ASSERT_EQUAL_INT32(1, buf[3].tag);
    TEST_ASSERT_EQUAL_INT32(2, buf[4].key);
    TEST_ASSERT_EQUAL_INT32(2, buf[4].tag);
}

static void test_insertion_sort_stays_within_the_subspan() {
    int32_t buf[5] = {9, 3, 1, 2, 9};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    tda_span_insertion_sort(tda_span_sub_mut(s, 1, 3), tda_cmp_i32);

    constexpr int32_t expected[5] = {9, 1, 2, 3, 9};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 5);
}

typedef struct {
    alignas(32) double key;
} Wide;

// addresses, not pointers: ordering a pointer outside the array against one inside it
// is itself undefined, and outside is exactly the case being looked for
static uintptr_t wide_first = 0;
static uintptr_t wide_end = 0;
static size_t wide_strays = 0;

static int cmp_wide_in_place(const void *lhs, const void *rhs) {
    wide_strays += (uintptr_t) lhs < wide_first || (uintptr_t) lhs >= wide_end;
    wide_strays += (uintptr_t) rhs < wide_first || (uintptr_t) rhs >= wide_end;

    const Wide *a = lhs;
    const Wide *b = rhs;
    return (a->key > b->key) - (a->key < b->key);
}

// the comparator reads elems as their own type, so it may only ever be handed pointers
// into the span: a copy set aside on the stack is aligned for max_align_t at best, which
// an elem like this one outgrows
static void test_insertion_sort_compares_elems_where_they_lie() {
    Wide buf[12];
    for (size_t i = 0; i < 12; ++i) {
        buf[i] = (Wide){.key = (double) (12 - i)};
    }

    wide_first = (uintptr_t) buf;
    wide_end = (uintptr_t) (buf + 12);
    wide_strays = 0;
    tda_span_insertion_sort(TDA_SPAN_FROM_DATA_MUT(Wide, buf, 12), cmp_wide_in_place);

    TEST_ASSERT_EQUAL_size_t(0, wide_strays);
    for (size_t i = 0; i < 12; ++i) {
        TEST_ASSERT_EQUAL_DOUBLE((double) (i + 1), buf[i].key);
    }
}

typedef struct {
    int32_t key;
    unsigned char payload[300];
} Bulky;

static int cmp_bulky(const void *lhs, const void *rhs) {
    return tda_cmp_i32(&((const Bulky *) lhs)->key, &((const Bulky *) rhs)->key);
}

// too big to be set aside, so it takes the path that swaps instead of sliding
static void test_insertion_sort_moves_elems_too_big_to_set_aside() {
    static Bulky buf[8];
    for (size_t i = 0; i < 8; ++i) {
        buf[i].key = (int32_t) (8 - i);
        memset(buf[i].payload, (int) (8 - i), sizeof(buf[i].payload));
    }

    tda_span_insertion_sort(TDA_SPAN_FROM_DATA_MUT(Bulky, buf, 8), cmp_bulky);

    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_INT32((int32_t) (i + 1), buf[i].key);
        TEST_ASSERT_EQUAL_UINT8(i + 1, buf[i].payload[0]);
        TEST_ASSERT_EQUAL_UINT8(i + 1, buf[i].payload[299]);
    }
}

/* ========== sort ========== */

static void test_sort_orders_a_shuffled_span() {
    int32_t buf[6] = {5, 3, 1, 4, 2, 6};

    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), tda_cmp_i32);

    constexpr int32_t want[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 6);
}

static void test_sort_keeps_duplicates() {
    int32_t buf[6] = {3, 1, 3, 2, 1, 3};

    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), tda_cmp_i32);

    constexpr int32_t want[6] = {1, 1, 2, 3, 3, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 6);
}

static void test_sort_empty_and_single_are_noop() {
    int32_t buf[1] = {42};

    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 0), tda_cmp_i32);
    TEST_ASSERT_EQUAL_INT32(42, buf[0]);

    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 1), tda_cmp_i32);
    TEST_ASSERT_EQUAL_INT32(42, buf[0]);
}

static void test_sort_follows_the_comparator() {
    int32_t buf[5] = {2, 5, 1, 4, 3};

    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), tda_cmp_desc_i32);

    constexpr int32_t want[5] = {5, 4, 3, 2, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

static void test_sort_stays_within_the_subspan() {
    int32_t buf[5] = {9, 3, 1, 2, 9};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    tda_span_sort(tda_span_sub_mut(s, 1, 3), tda_cmp_i32);

    constexpr int32_t want[5] = {9, 1, 2, 3, 9};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

// long enough to exercise the real partitioning rather than a base case
static void test_sort_orders_a_long_span() {
    int32_t buf[64];
    for (size_t i = 0; i < 64; ++i) {
        // 37 is coprime with 64, so this is a scrambled permutation of 0..63
        buf[i] = (int32_t) ((i * 37 + 11) % 64);
    }

    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 64);
    TEST_ASSERT_FALSE(tda_span_is_sorted(tda_span_mut_to_span(s), tda_cmp_i32));

    tda_span_sort(s, tda_cmp_i32);

    TEST_ASSERT_TRUE(tda_span_is_sorted(tda_span_mut_to_span(s), tda_cmp_i32));
    for (size_t i = 0; i < 64; ++i) {
        TEST_ASSERT_EQUAL_INT32((int32_t) i, buf[i]);
    }
}

// elem_size drives the moves, so a type wider than a word must travel whole
static void test_sort_moves_whole_elems() {
    Tagged buf[3] = {{3, 30}, {1, 10}, {2, 20}};

    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(Tagged, buf, 3), cmp_tagged);

    TEST_ASSERT_EQUAL_INT32(1, buf[0].key);
    TEST_ASSERT_EQUAL_INT32(10, buf[0].tag);
    TEST_ASSERT_EQUAL_INT32(2, buf[1].key);
    TEST_ASSERT_EQUAL_INT32(20, buf[1].tag);
    TEST_ASSERT_EQUAL_INT32(3, buf[2].key);
    TEST_ASSERT_EQUAL_INT32(30, buf[2].tag);
}

/* ========== sort_stable ========== */

static void test_sort_stable_orders_a_shuffled_span() {
    int32_t buf[6] = {5, 3, 1, 4, 2, 6};

    TEST_ASSERT_EQUAL_INT(
        TDA_STATUS_OK,
        tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), tda_cmp_i32, tda_al_default())
    );

    constexpr int32_t want[6] = {1, 2, 3, 4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 6);
}

// the reason this variant exists: equal keys must keep their original order
static void test_sort_stable_keeps_equal_elems_in_order() {
    Tagged buf[6] = {
        {2, 0}, {1, 0}, {2, 1}, {1, 1}, {2, 2}, {1, 2},
    };

    TEST_ASSERT_EQUAL_INT(
        TDA_STATUS_OK,
        tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(Tagged, buf, 6), cmp_tagged, tda_al_default())
    );

    for (size_t i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_INT32(1, buf[i].key);
        TEST_ASSERT_EQUAL_INT32((int32_t) i, buf[i].tag);
    }
    for (size_t i = 3; i < 6; ++i) {
        TEST_ASSERT_EQUAL_INT32(2, buf[i].key);
        TEST_ASSERT_EQUAL_INT32((int32_t) (i - 3), buf[i].tag);
    }
}

// the bottom-up merge walks runs of 1, 2, 4, ...; a length that is not a power of two
// leaves a short tail run on every pass, which is where the bounds get interesting
static void test_sort_stable_handles_lengths_that_are_not_powers_of_two() {
    for (size_t len = 1; len <= 11; ++len) {
        int32_t buf[11];
        for (size_t i = 0; i < len; ++i) {
            buf[i] = (int32_t) ((i * 7 + 3) % 11);
        }

        int32_t want[11];
        sorted_copy(want, buf, len);

        TEST_ASSERT_EQUAL_INT(
            TDA_STATUS_OK,
            tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, len), tda_cmp_i32, tda_al_default())
        );

        TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, len);
    }
}

static void test_sort_stable_stays_within_the_subspan() {
    int32_t buf[5] = {9, 3, 1, 2, 9};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    TEST_ASSERT_EQUAL_INT(
        TDA_STATUS_OK,
        tda_span_sort_stable(tda_span_sub_mut(s, 1, 3), tda_cmp_i32, tda_al_default())
    );

    constexpr int32_t want[5] = {9, 1, 2, 3, 9};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

static void test_sort_stable_works_through_an_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    int32_t buf[5] = {5, 4, 3, 2, 1};

    TEST_ASSERT_EQUAL_INT(
        TDA_STATUS_OK,
        tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), tda_cmp_i32, arena)
    );

    constexpr int32_t want[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);

    tda_al_arena_drop(arena);
}

// the scratch buffer cannot be had: reported, and the span is left alone
static void test_sort_stable_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 16);
    TEST_ASSERT_NOT_NULL(arena);

    int32_t buf[8] = {8, 7, 6, 5, 4, 3, 2, 1};

    TEST_ASSERT_EQUAL_INT(
        TDA_STATUS_ERR_NO_MEM,
        tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 8), tda_cmp_i32, arena)
    );

    constexpr int32_t want[8] = {8, 7, 6, 5, 4, 3, 2, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 8);

    tda_al_arena_drop(arena);
}

// nothing to merge means nothing to allocate — an allocator with no room must still do
static void test_sort_stable_of_empty_and_single_needs_no_scratch() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 16);
    TEST_ASSERT_NOT_NULL(arena);

    int32_t buf[1] = {42};

    TEST_ASSERT_EQUAL_INT(
        TDA_STATUS_OK,
        tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 0), tda_cmp_i32, arena)
    );
    TEST_ASSERT_EQUAL_INT(
        TDA_STATUS_OK,
        tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 1), tda_cmp_i32, arena)
    );

    TEST_ASSERT_EQUAL_INT32(42, buf[0]);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

/* ========== partial_sort ========== */

static void test_partial_sort_puts_the_n_smallest_in_order_at_the_front() {
    int32_t buf[8] = {7, 2, 8, 1, 5, 3, 6, 4};
    constexpr int32_t src[8] = {7, 2, 8, 1, 5, 3, 6, 4};

    tda_span_partial_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 8), 3, tda_cmp_i32);

    constexpr int32_t want_head[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want_head, buf, 3);

    // the tail is whatever is left, but nothing may go missing
    TEST_ASSERT_TRUE(same_elems(src, buf, 8));

    // and nothing smaller than the head may hide in the tail
    for (size_t i = 3; i < 8; ++i) {
        TEST_ASSERT_TRUE(buf[i] > buf[2]);
    }
}

static void test_partial_sort_with_n_zero_is_a_noop() {
    int32_t buf[4] = {4, 3, 2, 1};

    tda_span_partial_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4), 0, tda_cmp_i32);

    constexpr int32_t want[4] = {4, 3, 2, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 4);
}

static void test_partial_sort_with_n_equal_to_len_sorts_everything() {
    int32_t buf[5] = {3, 5, 1, 4, 2};

    tda_span_partial_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), 5, tda_cmp_i32);

    constexpr int32_t want[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, buf, 5);
}

static void test_partial_sort_follows_the_comparator() {
    int32_t buf[6] = {3, 6, 1, 5, 2, 4};

    tda_span_partial_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6), 2, tda_cmp_desc_i32);

    constexpr int32_t want_head[2] = {6, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want_head, buf, 2);
}

static void test_partial_sort_handles_duplicates() {
    int32_t buf[7] = {3, 1, 3, 1, 2, 3, 2};
    constexpr int32_t src[7] = {3, 1, 3, 1, 2, 3, 2};

    tda_span_partial_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7), 4, tda_cmp_i32);

    constexpr int32_t want_head[4] = {1, 1, 2, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want_head, buf, 4);
    TEST_ASSERT_TRUE(same_elems(src, buf, 7));
}

/* ========== nth_elem ========== */

// every position in one sweep: whatever lands at nth must be what a full sort would
// have put there
static void test_nth_elem_places_every_position() {
    constexpr int32_t src[7] = {6, 2, 7, 1, 5, 3, 4};

    int32_t want[7];
    sorted_copy(want, src, 7);

    for (size_t nth = 0; nth < 7; ++nth) {
        int32_t buf[7];
        memcpy(buf, src, sizeof(buf));

        tda_span_nth_elem(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7), nth, tda_cmp_i32);

        TEST_ASSERT_EQUAL_INT32(want[nth], buf[nth]);
        TEST_ASSERT_TRUE(same_elems(src, buf, 7));
    }
}

// the point of the operation: the span is split around nth, even though neither side
// is ordered within itself
static void test_nth_elem_partitions_around_the_nth() {
    int32_t buf[9] = {9, 1, 8, 2, 7, 3, 6, 4, 5};
    constexpr size_t nth = 4;

    tda_span_nth_elem(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 9), nth, tda_cmp_i32);

    for (size_t i = 0; i < nth; ++i) {
        TEST_ASSERT_TRUE(buf[i] <= buf[nth]);
    }
    for (size_t i = nth + 1; i < 9; ++i) {
        TEST_ASSERT_TRUE(buf[i] >= buf[nth]);
    }
}

static void test_nth_elem_of_a_single_is_a_noop() {
    int32_t buf[1] = {42};

    tda_span_nth_elem(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 1), 0, tda_cmp_i32);

    TEST_ASSERT_EQUAL_INT32(42, buf[0]);
}

// equal elements make many positions interchangeable; the value at nth is still fixed
static void test_nth_elem_handles_duplicates() {
    constexpr int32_t src[8] = {2, 1, 2, 1, 2, 1, 2, 1};

    int32_t want[8];
    sorted_copy(want, src, 8);

    for (size_t nth = 0; nth < 8; ++nth) {
        int32_t buf[8];
        memcpy(buf, src, sizeof(buf));

        tda_span_nth_elem(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 8), nth, tda_cmp_i32);

        TEST_ASSERT_EQUAL_INT32(want[nth], buf[nth]);
    }
}

static void test_nth_elem_stays_within_the_subspan() {
    int32_t buf[6] = {9, 4, 1, 3, 2, 9};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6);

    tda_span_nth_elem(tda_span_sub_mut(s, 1, 4), 0, tda_cmp_i32);

    TEST_ASSERT_EQUAL_INT32(9, buf[0]);
    TEST_ASSERT_EQUAL_INT32(1, buf[1]); // the smallest of the four
    TEST_ASSERT_EQUAL_INT32(9, buf[5]);
}

/* ========== scaling ==========
 *
 * A bad pivot choice still produces the right answer, just after far too many
 * comparisons — no assertion about the output can see it. These count the comparisons
 * instead. 65536 is 2^16, so n * log2(n) is exact and needs no math.h.
 *
 * The size is not arbitrary. Sampling the pivot only at the ends degrades to about
 * n^1.5, which at n = 4096 is still within 4x of n*log n and hides; measured against
 * this implementation it is 4.0x at 4096, 6.6x at 16384 and 11.3x at 65536, against
 * 1.0x here. The counts are fully deterministic, so the margin is real, not statistical.
 */

static constexpr size_t SCALE_N = 65536;
static constexpr size_t SCALE_N_LOG_N = SCALE_N * 16;
static constexpr size_t SCALE_LIMIT = 3 * SCALE_N_LOG_N;

static size_t cmp_calls = 0;

static int cmp_i32_counting(const void *a, const void *b) {
    ++cmp_calls;
    return tda_cmp_i32(a, b);
}

static void test_sort_stays_n_log_n_on_ordered_input() {
    static int32_t buf[SCALE_N];

    for (size_t i = 0; i < SCALE_N; ++i) {
        buf[i] = (int32_t) i;
    }
    cmp_calls = 0;
    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), cmp_i32_counting);
    TEST_ASSERT_LESS_THAN_size_t(SCALE_LIMIT, cmp_calls);

    for (size_t i = 0; i < SCALE_N; ++i) {
        buf[i] = (int32_t) (SCALE_N - i);
    }
    cmp_calls = 0;
    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), cmp_i32_counting);
    TEST_ASSERT_LESS_THAN_size_t(SCALE_LIMIT, cmp_calls);
}

// a run of equal keys must be settled in one pass, not peeled off one elem at a time
static void test_sort_stays_linear_on_equal_keys() {
    static int32_t buf[SCALE_N];
    for (size_t i = 0; i < SCALE_N; ++i) {
        buf[i] = 7;
    }

    cmp_calls = 0;
    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), cmp_i32_counting);

    TEST_ASSERT_LESS_THAN_size_t(8 * SCALE_N, cmp_calls);
}

// nth_elem is expected linear, and shares the pivot choice and the partition with sort
static void test_nth_elem_stays_linear() {
    static int32_t buf[SCALE_N];

    for (size_t i = 0; i < SCALE_N; ++i) {
        buf[i] = (int32_t) i;
    }
    cmp_calls = 0;
    tda_span_nth_elem(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), SCALE_N / 2, cmp_i32_counting);
    TEST_ASSERT_LESS_THAN_size_t(8 * SCALE_N, cmp_calls);

    for (size_t i = 0; i < SCALE_N; ++i) {
        buf[i] = 7;
    }
    cmp_calls = 0;
    tda_span_nth_elem(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), SCALE_N / 2, cmp_i32_counting);
    TEST_ASSERT_LESS_THAN_size_t(8 * SCALE_N, cmp_calls);
}

// McIlroy, "A Killer Adversary for Quicksort" (1999). A value is settled only when the
// sort first compares it, and always so that the pivot just picked turns out as bad as
// it can be. Every quicksort that picks its pivot deterministically goes quadratic
// against it, ninther included; only the depth cap and its heapsort fallback save one
static int32_t *adversary_val = nullptr;
static int32_t adversary_gas = 0;
static int32_t adversary_solid = 0;
static int32_t adversary_candidate = 0;

static int cmp_adversary(const void *lhs, const void *rhs) {
    ++cmp_calls;
    const int32_t a = *(const int32_t *) lhs;
    const int32_t b = *(const int32_t *) rhs;

    if (adversary_val[a] == adversary_gas && adversary_val[b] == adversary_gas) {
        adversary_val[a == adversary_candidate ? a : b] = adversary_solid++;
    }
    if (adversary_val[a] == adversary_gas) {
        adversary_candidate = a;
    } else if (adversary_val[b] == adversary_gas) {
        adversary_candidate = b;
    }
    return (adversary_val[a] > adversary_val[b]) - (adversary_val[a] < adversary_val[b]);
}

// the elems are the indices 0..SCALE_N-1, their values in 'val' all still undecided
static void adversary_arm(int32_t *buf, int32_t *val) {
    adversary_val = val;
    adversary_gas = SCALE_N - 1;
    adversary_solid = 0;
    adversary_candidate = 0;
    for (size_t i = 0; i < SCALE_N; ++i) {
        buf[i] = (int32_t) i;
        val[i] = adversary_gas;
    }
}

// 3.7x n*log n with the cap. Without it 29x at 4096, 98x at 16384 and 342x here
static void test_sort_stays_n_log_n_against_an_adversary() {
    static int32_t buf[SCALE_N];
    static int32_t val[SCALE_N];

    adversary_arm(buf, val);

    cmp_calls = 0;
    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), cmp_adversary);

    TEST_ASSERT_LESS_THAN_size_t(2 * SCALE_LIMIT, cmp_calls);
    for (size_t i = 1; i < SCALE_N; ++i) {
        TEST_ASSERT_TRUE(val[buf[i - 1]] <= val[buf[i]]);
    }
}

// the same hole in nth_elem, which partial_sort goes through: 268M comparisons here
// without the cap, 5.9M with it
static void test_nth_elem_stays_n_log_n_against_an_adversary() {
    static int32_t buf[SCALE_N];
    static int32_t val[SCALE_N];
    adversary_arm(buf, val);

    constexpr size_t nth = SCALE_N / 2;
    cmp_calls = 0;
    tda_span_nth_elem(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), nth, cmp_adversary);

    TEST_ASSERT_LESS_THAN_size_t(8 * SCALE_N_LOG_N, cmp_calls);
    for (size_t i = 0; i < nth; ++i) {
        TEST_ASSERT_TRUE(val[buf[i]] <= val[buf[nth]]);
    }
    for (size_t i = nth + 1; i < SCALE_N; ++i) {
        TEST_ASSERT_TRUE(val[buf[i]] >= val[buf[nth]]);
    }
}

// 269M without the cap in nth_elem, 6.4M with it
static void test_partial_sort_stays_n_log_n_against_an_adversary() {
    static int32_t buf[SCALE_N];
    static int32_t val[SCALE_N];
    adversary_arm(buf, val);

    constexpr size_t count = SCALE_N / 2;
    cmp_calls = 0;
    tda_span_partial_sort(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SCALE_N), count, cmp_adversary);

    TEST_ASSERT_LESS_THAN_size_t(8 * SCALE_N_LOG_N, cmp_calls);
    for (size_t i = 1; i < count; ++i) {
        TEST_ASSERT_TRUE(val[buf[i - 1]] <= val[buf[i]]);
    }
    for (size_t i = count; i < SCALE_N; ++i) {
        TEST_ASSERT_TRUE(val[buf[i]] >= val[buf[count - 1]]);
    }
}

/* ========== is_sorted ========== */

static void test_is_sorted_accepts_ascending() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};

    TEST_ASSERT_TRUE(tda_span_is_sorted(TDA_SPAN_FROM_DATA(int32_t, buf, 4), tda_cmp_i32));
}

static void test_is_sorted_allows_equal_neighbours() {
    constexpr int32_t buf[4] = {1, 2, 2, 3};

    TEST_ASSERT_TRUE(tda_span_is_sorted(TDA_SPAN_FROM_DATA(int32_t, buf, 4), tda_cmp_i32));
}

// the break of order is in the last pair — the walk must reach it
static void test_is_sorted_rejects_a_late_inversion() {
    constexpr int32_t buf[4] = {1, 2, 3, 0};

    TEST_ASSERT_FALSE(tda_span_is_sorted(TDA_SPAN_FROM_DATA(int32_t, buf, 4), tda_cmp_i32));
}

static void test_is_sorted_empty_and_single_are_sorted() {
    constexpr int32_t buf[1] = {42};

    TEST_ASSERT_TRUE(tda_span_is_sorted(TDA_SPAN_FROM_DATA(int32_t, buf, 0), tda_cmp_i32));
    TEST_ASSERT_TRUE(tda_span_is_sorted(TDA_SPAN_FROM_DATA(int32_t, buf, 1), tda_cmp_i32));
}

static void test_is_sorted_agrees_with_sort() {
    int32_t buf[6] = {4, 1, 6, 2, 5, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6);

    TEST_ASSERT_FALSE(tda_span_is_sorted(tda_span_mut_to_span(s), tda_cmp_i32));
    tda_span_insertion_sort(s, tda_cmp_i32);
    TEST_ASSERT_TRUE(tda_span_is_sorted(tda_span_mut_to_span(s), tda_cmp_i32));
}

/* ========== is_sorted_until ========== */

static void test_is_sorted_until_points_at_the_first_break() {
    constexpr int32_t buf[6] = {1, 2, 3, 2, 5, 6};

    TEST_ASSERT_EQUAL_size_t(3, tda_span_is_sorted_until(TDA_SPAN_FROM_DATA(int32_t, buf, 6), tda_cmp_i32));
}

// a sorted span answers with its own length, which is what makes
// is_sorted a one-liner over it
static void test_is_sorted_until_of_a_sorted_span_is_its_length() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 4);

    TEST_ASSERT_EQUAL_size_t(4, tda_span_is_sorted_until(s, tda_cmp_i32));
    TEST_ASSERT_TRUE(tda_span_is_sorted(s, tda_cmp_i32));
}

static void test_is_sorted_until_on_short_and_flat_spans() {
    constexpr int32_t one[1] = {5};
    constexpr int32_t flat[3] = {7, 7, 7};

    TEST_ASSERT_EQUAL_size_t(0, tda_span_is_sorted_until(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0), tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(1, tda_span_is_sorted_until(TDA_SPAN_FROM_DATA(int32_t, one, 1), tda_cmp_i32));
    TEST_ASSERT_EQUAL_size_t(3, tda_span_is_sorted_until(TDA_SPAN_FROM_DATA(int32_t, flat, 3), tda_cmp_i32));
}

static void test_is_sorted_until_breaks_at_the_second_elem() {
    constexpr int32_t buf[3] = {9, 1, 2};

    TEST_ASSERT_EQUAL_size_t(1, tda_span_is_sorted_until(TDA_SPAN_FROM_DATA(int32_t, buf, 3), tda_cmp_i32));
}

static void test_is_sorted_until_follows_the_comparator() {
    constexpr int32_t buf[4] = {4, 3, 2, 9};

    TEST_ASSERT_EQUAL_size_t(3, tda_span_is_sorted_until(TDA_SPAN_FROM_DATA(int32_t, buf, 4), tda_cmp_desc_i32));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_insertion_sort_orders_a_shuffled_span);
    RUN_TEST(test_insertion_sort_already_sorted_is_unchanged);
    RUN_TEST(test_insertion_sort_reversed_span);
    RUN_TEST(test_insertion_sort_keeps_duplicates);
    RUN_TEST(test_insertion_sort_empty_and_single_are_noop);
    RUN_TEST(test_insertion_sort_follows_the_comparator);
    RUN_TEST(test_insertion_sort_is_stable);
    RUN_TEST(test_insertion_sort_stays_within_the_subspan);
    RUN_TEST(test_insertion_sort_compares_elems_where_they_lie);
    RUN_TEST(test_insertion_sort_moves_elems_too_big_to_set_aside);

    RUN_TEST(test_sort_orders_a_shuffled_span);
    RUN_TEST(test_sort_keeps_duplicates);
    RUN_TEST(test_sort_empty_and_single_are_noop);
    RUN_TEST(test_sort_follows_the_comparator);
    RUN_TEST(test_sort_stays_within_the_subspan);
    RUN_TEST(test_sort_orders_a_long_span);
    RUN_TEST(test_sort_moves_whole_elems);

    RUN_TEST(test_sort_stable_orders_a_shuffled_span);
    RUN_TEST(test_sort_stable_keeps_equal_elems_in_order);
    RUN_TEST(test_sort_stable_handles_lengths_that_are_not_powers_of_two);
    RUN_TEST(test_sort_stable_stays_within_the_subspan);
    RUN_TEST(test_sort_stable_works_through_an_arena);
    RUN_TEST(test_sort_stable_reports_an_exhausted_arena);
    RUN_TEST(test_sort_stable_of_empty_and_single_needs_no_scratch);

    RUN_TEST(test_partial_sort_puts_the_n_smallest_in_order_at_the_front);
    RUN_TEST(test_partial_sort_with_n_zero_is_a_noop);
    RUN_TEST(test_partial_sort_with_n_equal_to_len_sorts_everything);
    RUN_TEST(test_partial_sort_follows_the_comparator);
    RUN_TEST(test_partial_sort_handles_duplicates);

    RUN_TEST(test_nth_elem_places_every_position);
    RUN_TEST(test_nth_elem_partitions_around_the_nth);
    RUN_TEST(test_nth_elem_of_a_single_is_a_noop);
    RUN_TEST(test_nth_elem_handles_duplicates);
    RUN_TEST(test_nth_elem_stays_within_the_subspan);

    RUN_TEST(test_sort_stays_n_log_n_on_ordered_input);
    RUN_TEST(test_sort_stays_linear_on_equal_keys);
    RUN_TEST(test_nth_elem_stays_linear);
    RUN_TEST(test_sort_stays_n_log_n_against_an_adversary);
    RUN_TEST(test_nth_elem_stays_n_log_n_against_an_adversary);
    RUN_TEST(test_partial_sort_stays_n_log_n_against_an_adversary);

    RUN_TEST(test_is_sorted_accepts_ascending);
    RUN_TEST(test_is_sorted_allows_equal_neighbours);
    RUN_TEST(test_is_sorted_rejects_a_late_inversion);
    RUN_TEST(test_is_sorted_empty_and_single_are_sorted);
    RUN_TEST(test_is_sorted_agrees_with_sort);

    RUN_TEST(test_is_sorted_until_points_at_the_first_break);
    RUN_TEST(test_is_sorted_until_of_a_sorted_span_is_its_length);
    RUN_TEST(test_is_sorted_until_on_short_and_flat_spans);
    RUN_TEST(test_is_sorted_until_breaks_at_the_second_elem);
    RUN_TEST(test_is_sorted_until_follows_the_comparator);

    return UNITY_END();
}
