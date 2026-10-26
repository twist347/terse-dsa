// Every callback gets the ctx passed beside it, unchanged, on every call: through the
// algos, through the containers that store a callback, and through the copies, moves
// and swaps that carry one along. Each test uses a ctx a comparator cannot do without.

#include "tda/algo/compare.h"
#include "tda/algo/copy.h"
#include "tda/algo/fill.h"
#include "tda/algo/fold.h"
#include "tda/algo/heap.h"
#include "tda/algo/merge.h"
#include "tda/algo/modify.h"
#include "tda/algo/permute.h"
#include "tda/algo/search.h"
#include "tda/algo/set.h"
#include "tda/algo/sort.h"
#include "tda/algo/transform.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/ds/arr.h"
#include "tda/ds/deque.h"
#include "tda/ds/hmap.h"
#include "tda/ds/hset.h"
#include "tda/ds/list.h"
#include "tda/ds/pqueue.h"
#include "tda/ds/queue.h"
#include "tda/ds/stack.h"
#include "tda/ds/vec.h"

#include "support/status.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

/* ========== callbacks that need their ctx ========== */

// orders indices by the keys they point at; the keys are the ctx
static int cmp_by_key(const void *lhs, const void *rhs, void *ctx) {
    const int32_t *keys = ctx;

    return tda_cmp_i32(&keys[*(const size_t *) lhs], &keys[*(const size_t *) rhs], nullptr);
}

// equal when equal modulo the ctx
static bool eq_mod(const void *lhs, const void *rhs, void *ctx) {
    const int32_t m = *(const int32_t *) ctx;

    return *(const int32_t *) lhs % m == *(const int32_t *) rhs % m;
}

// a hash that agrees with eq_mod, so keys equal modulo the ctx land together
static tda_Hash hash_mod(const void *key, void *ctx) {
    const int32_t m = *(const int32_t *) ctx;
    const int32_t r = *(const int32_t *) key % m;

    return tda_hash_i32(&r, nullptr);
}

// a hash salted by the ctx, beside an equality that needs none: two seeds, one meaning
static tda_Hash hash_seeded(const void *key, void *ctx) {
    return tda_hash_i32(key, nullptr) ^ *(const tda_Hash *) ctx;
}

/* ========== algo ========== */

// argsort: the span holds indices, the order comes from elsewhere
static void test_sort_orders_indices_by_keys_in_the_ctx() {
    int32_t keys[5] = {40, 10, 30, 50, 20};
    size_t idx[5] = {0, 1, 2, 3, 4};

    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(size_t, idx, 5), cmp_by_key, keys);

    constexpr size_t want[5] = {1, 4, 2, 0, 3};
    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_size_t(want[i], idx[i]);
    }
}

static void test_sort_stable_and_binary_search_take_it_too() {
    int32_t keys[6] = {3, 1, 3, 2, 1, 2};
    size_t idx[6] = {0, 1, 2, 3, 4, 5};

    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(size_t, idx, 6);
    TDA_TEST_OK(tda_span_sort_stable(s, cmp_by_key, keys, tda_al_default()));

    // stable: equal keys keep their index order
    constexpr size_t want[6] = {1, 4, 3, 5, 0, 2};
    for (size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_size_t(want[i], idx[i]);
    }

    // the probe is an index too, here of a key of 2
    constexpr size_t probe = 3;
    size_t at;
    TEST_ASSERT_TRUE(tda_span_binary_search(tda_span_mut_to_span(s), &probe, cmp_by_key, keys, &at));
    TEST_ASSERT_EQUAL_INT32(2, keys[idx[at]]);
}

static void test_find_matches_under_the_ctx() {
    constexpr int32_t buf[4] = {1, 12, 23, 34};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 4);
    constexpr int32_t key = 44;
    int32_t ten = 10, eleven = 11;

    size_t at;
    TEST_ASSERT_TRUE(tda_span_find(s, &key, eq_mod, &ten, &at));
    TEST_ASSERT_EQUAL_size_t(3, at);

    // modulo 11, 44 is 0 and none of them is
    TEST_ASSERT_FALSE(tda_span_find(s, &key, eq_mod, &eleven, &at));
}

// the keys every order below is read from: an index sorts by keys[index]
static int32_t KEYS[8] = {70, 20, 50, 10, 80, 40, 30, 60};

static size_t key_rank_of(size_t idx) {
    size_t rank = 0;
    for (size_t i = 0; i < 8; ++i) {
        rank += KEYS[i] < KEYS[idx];
    }

    return rank;
}

// heaps, partial and nth sorts, bounds and extremes all reach the keys through the ctx
static void test_the_ordering_algos_read_the_ctx() {
    size_t idx[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(size_t, idx, 8);

    tda_span_make_heap(s, cmp_by_key, KEYS);
    TEST_ASSERT_TRUE(tda_span_is_heap(tda_span_mut_to_span(s), cmp_by_key, KEYS));
    TEST_ASSERT_EQUAL_size_t(4, idx[0]); // the greatest key, 80

    tda_span_partial_sort(s, 3, cmp_by_key, KEYS);
    TEST_ASSERT_EQUAL_size_t(3, idx[0]);
    TEST_ASSERT_EQUAL_size_t(1, idx[1]);
    TEST_ASSERT_EQUAL_size_t(6, idx[2]);

    tda_span_nth_elem(s, 5, cmp_by_key, KEYS);
    TEST_ASSERT_EQUAL_size_t(5, key_rank_of(idx[5]));

    tda_span_sort(s, cmp_by_key, KEYS);
    const tda_Span v = tda_span_mut_to_span(s);
    constexpr size_t probe = 2; // key 50
    TEST_ASSERT_EQUAL_size_t(4, tda_span_lower_bound(v, &probe, cmp_by_key, KEYS));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_min_elem(v, cmp_by_key, KEYS));
}

// merging and set ops compare across two runs, both sorted under the ctx
static void test_the_merging_algos_read_the_ctx() {
    constexpr size_t a[4] = {3, 6, 0, 4};  // keys 10 30 70 80
    constexpr size_t b[4] = {1, 5, 2, 7};  // keys 20 40 50 60
    size_t out[8];

    tda_span_merge(
        TDA_SPAN_FROM_DATA_MUT(size_t, out, 8),
        TDA_SPAN_FROM_DATA(size_t, a, 4),
        TDA_SPAN_FROM_DATA(size_t, b, 4),
        cmp_by_key,
        KEYS
    );
    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_size_t(i, key_rank_of(out[i]));
    }

    size_t both[8] = {3, 6, 0, 4, 1, 5, 2, 7};
    tda_span_inplace_merge(TDA_SPAN_FROM_DATA_MUT(size_t, both, 8), 4, cmp_by_key, KEYS, tda_al_default());
    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_size_t(i, key_rank_of(both[i]));
    }

    const size_t n = tda_span_set_union(
        TDA_SPAN_FROM_DATA_MUT(size_t, out, 8),
        TDA_SPAN_FROM_DATA(size_t, a, 4),
        TDA_SPAN_FROM_DATA(size_t, b, 4),
        cmp_by_key,
        KEYS
    );
    TEST_ASSERT_EQUAL_size_t(8, n);
    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_size_t(i, key_rank_of(out[i]));
    }

    constexpr size_t c[2] = {6, 4}; // keys 30 80
    const size_t diff = tda_span_set_difference(
        TDA_SPAN_FROM_DATA_MUT(size_t, out, 8),
        TDA_SPAN_FROM_DATA(size_t, a, 4),
        TDA_SPAN_FROM_DATA(size_t, c, 2),
        cmp_by_key,
        KEYS
    );
    TEST_ASSERT_EQUAL_size_t(2, diff);
    TEST_ASSERT_EQUAL_size_t(3, out[0]);
    TEST_ASSERT_EQUAL_size_t(0, out[1]);

    constexpr size_t d[2] = {5, 4}; // keys 40 80
    const size_t sym = tda_span_set_symmetric_difference(
        TDA_SPAN_FROM_DATA_MUT(size_t, out, 8),
        TDA_SPAN_FROM_DATA(size_t, a, 4),
        TDA_SPAN_FROM_DATA(size_t, d, 2),
        cmp_by_key,
        KEYS
    );
    constexpr size_t want[4] = {3, 6, 5, 0}; // keys 10 30 40 70
    TEST_ASSERT_EQUAL_size_t(4, sym);
    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_size_t(want[i], out[i]);
    }
}

// the searches that walk one span against itself or against a second one
static void test_the_searching_algos_read_the_ctx() {
    constexpr int32_t buf[5] = {1, 11, 21, 5, 15};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 5);
    int32_t ten = 10;
    size_t at;

    TEST_ASSERT_TRUE(tda_span_find_run(s, &(int32_t){31}, 3, eq_mod, &ten, &at));
    TEST_ASSERT_EQUAL_size_t(0, at);

    constexpr int32_t set[1] = {25};
    TEST_ASSERT_TRUE(tda_span_find_any_of(s, TDA_SPAN_FROM_DATA(int32_t, set, 1), eq_mod, &ten, &at));
    TEST_ASSERT_EQUAL_size_t(3, at);

    TEST_ASSERT_TRUE(tda_span_find_adjacent(tda_span_sub(s, 2, 3), eq_mod, &ten, &at));
    TEST_ASSERT_EQUAL_size_t(1, at); // 5 and 15

    constexpr int32_t sub[2] = {35, 45};
    TEST_ASSERT_TRUE(tda_span_find_sub(s, TDA_SPAN_FROM_DATA(int32_t, sub, 2), eq_mod, &ten, &at));
    TEST_ASSERT_EQUAL_size_t(3, at);
    TEST_ASSERT_TRUE(tda_span_ends_with(s, TDA_SPAN_FROM_DATA(int32_t, sub, 2), eq_mod, &ten));

    constexpr int32_t other[5] = {31, 41, 51, 6, 15};
    TEST_ASSERT_TRUE(tda_span_mismatch(s, TDA_SPAN_FROM_DATA(int32_t, other, 5), eq_mod, &ten, &at));
    TEST_ASSERT_EQUAL_size_t(3, at);

    // indices 0 and 1 compare by their keys, 70 and 20, not by themselves
    constexpr size_t lo[1] = {0};
    constexpr size_t hi[1] = {1};
    TEST_ASSERT_GREATER_THAN_INT(
        0,
        tda_span_cmp(TDA_SPAN_FROM_DATA(size_t, lo, 1), TDA_SPAN_FROM_DATA(size_t, hi, 1), cmp_by_key, KEYS)
    );

    constexpr size_t idx[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    const tda_Span v = TDA_SPAN_FROM_DATA(size_t, idx, 8);
    TEST_ASSERT_EQUAL_size_t(4, tda_span_max_elem(v, cmp_by_key, KEYS));
    const tda_MinMax mm = tda_span_minmax_elem(v, cmp_by_key, KEYS);
    TEST_ASSERT_EQUAL_size_t(3, mm.min);
    TEST_ASSERT_EQUAL_size_t(4, mm.max);
}

static void test_unique_and_permutations_read_the_ctx() {
    int32_t buf[5] = {1, 11, 21, 2, 12};
    int32_t ten = 10;
    TEST_ASSERT_EQUAL_size_t(2, tda_span_unique(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), eq_mod, &ten));
    TEST_ASSERT_EQUAL_INT32(1, buf[0]);
    TEST_ASSERT_EQUAL_INT32(2, buf[1]);

    // by keys 10 20 30 the indices are in order, so the next permutation swaps the last two
    int32_t keys[3] = {30, 10, 20};
    size_t idx[3] = {1, 2, 0};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(size_t, idx, 3);

    TEST_ASSERT_TRUE(tda_span_next_permutation(s, cmp_by_key, keys));
    TEST_ASSERT_EQUAL_size_t(1, idx[0]);
    TEST_ASSERT_EQUAL_size_t(0, idx[1]);
    TEST_ASSERT_EQUAL_size_t(2, idx[2]);

    TEST_ASSERT_TRUE(tda_span_prev_permutation(s, cmp_by_key, keys));
    TEST_ASSERT_EQUAL_size_t(1, idx[0]);
    TEST_ASSERT_EQUAL_size_t(2, idx[1]);
    TEST_ASSERT_EQUAL_size_t(0, idx[2]);
}

// every container's eq_by hands its ctx to the equality
static void test_every_eq_by_passes_the_ctx() {
    int32_t ten = 10;
    tda_Al al = tda_al_default();

    tda_Arr aa = {}, ab = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al, &aa, 1, 2));
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al, &ab, 11, 22));
    TEST_ASSERT_TRUE(tda_arr_eq_by(&aa, &ab, eq_mod, &ten));

    tda_Vec va = {}, vb = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, al, &va, 1, 2));
    TDA_TEST_OK(TDA_VEC_OF(int32_t, al, &vb, 11, 22));
    TEST_ASSERT_TRUE(tda_vec_eq_by(&va, &vb, eq_mod, &ten));

    tda_Deque da = {}, db = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, al, &da, 1, 2));
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, al, &db, 11, 22));
    TEST_ASSERT_TRUE(tda_deque_eq_by(&da, &db, eq_mod, &ten));

    tda_List la = {}, lb = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &la, 1, 2));
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &lb, 11, 22));
    TEST_ASSERT_TRUE(tda_list_eq_by(&la, &lb, eq_mod, &ten));

    tda_Queue qa = {}, qb = {};
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, al, &qa, 1, 2));
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, al, &qb, 11, 22));
    TEST_ASSERT_TRUE(tda_queue_eq_by(&qa, &qb, eq_mod, &ten));

    tda_Stack sa = {}, sb = {};
    TDA_TEST_OK(TDA_STACK_OF(int32_t, al, &sa, 1, 2));
    TDA_TEST_OK(TDA_STACK_OF(int32_t, al, &sb, 11, 22));
    TEST_ASSERT_TRUE(tda_stack_eq_by(&sa, &sb, eq_mod, &ten));

    tda_arr_deinit(&aa);
    tda_arr_deinit(&ab);
    tda_vec_deinit(&va);
    tda_vec_deinit(&vb);
    tda_deque_deinit(&da);
    tda_deque_deinit(&db);
    tda_list_deinit(&la);
    tda_list_deinit(&lb);
    tda_queue_deinit(&qa);
    tda_queue_deinit(&qb);
    tda_stack_deinit(&sa);
    tda_stack_deinit(&sb);
}

// the key forms of remove and replace wrap the equality in a predicate, and its ctx
// has to ride along inside
static void test_remove_and_replace_match_under_the_ctx() {
    int32_t buf[5] = {1, 11, 2, 21, 3};
    int32_t ten = 10;
    constexpr int32_t one = 1;
    constexpr int32_t two = 2;
    constexpr int32_t zero = 0;

    const size_t kept = tda_span_remove(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5), &one, eq_mod, &ten);
    TEST_ASSERT_EQUAL_size_t(2, kept);
    TEST_ASSERT_EQUAL_INT32(2, buf[0]);
    TEST_ASSERT_EQUAL_INT32(3, buf[1]);

    int32_t more[3] = {12, 5, 22};
    tda_span_replace(TDA_SPAN_FROM_DATA_MUT(int32_t, more, 3), &two, &zero, eq_mod, &ten);
    TEST_ASSERT_EQUAL_INT32(0, more[0]);
    TEST_ASSERT_EQUAL_INT32(5, more[1]);
    TEST_ASSERT_EQUAL_INT32(0, more[2]);
}

/* ========== every algo, on inputs that reach every path ========== */

// each callback below counts the calls that came without this ctx; a sweep large enough
// for the recursive and fallback paths then checks the count is zero
static int SWEEP;
static size_t sweep_calls;
static size_t sweep_wrong;

static void seen(const void *ctx) {
    ++sweep_calls;
    sweep_wrong += ctx != &SWEEP;
}

static int cmp_seen(const void *lhs, const void *rhs, void *ctx) {
    seen(ctx);

    return tda_cmp_i32(lhs, rhs, nullptr);
}

static bool eq_seen(const void *lhs, const void *rhs, void *ctx) {
    seen(ctx);

    return *(const int32_t *) lhs == *(const int32_t *) rhs;
}

static bool small_seen(const void *elem, void *ctx) {
    seen(ctx);

    return *(const int32_t *) elem < 100;
}

static void fold_seen(void *acc, const void *elem, void *ctx) {
    seen(ctx);
    *(int64_t *) acc += *(const int32_t *) elem;
}

static void gen_seen(void *dst, size_t idx, void *ctx) {
    seen(ctx);
    *(int32_t *) dst = (int32_t) idx;
}

static void double_seen(void *dst, const void *src, void *ctx) {
    seen(ctx);
    *(int32_t *) dst = 2 * *(const int32_t *) src;
}

// too big for insertion sort to set one aside, so it sorts by swaps
typedef struct {
    int32_t key;
    unsigned char pad[300];
} Big;

static int cmp_big_seen(const void *lhs, const void *rhs, void *ctx) {
    seen(ctx);

    return tda_cmp_i32(&((const Big *) lhs)->key, &((const Big *) rhs)->key, nullptr);
}

static void add_seen(void *dst, const void *a, const void *b, void *ctx) {
    seen(ctx);
    *(int32_t *) dst = *(const int32_t *) a + *(const int32_t *) b;
}

enum { SWEEP_N = 600 };

// past insertion sort's cutoff, with duplicates, so quicksort recurses and splits three
// ways
static void sweep_fill(int32_t *buf) {
    uint32_t state = 12345;
    for (size_t i = 0; i < SWEEP_N; ++i) {
        state = state * 1103515245u + 12345u;
        buf[i] = (int32_t) ((state >> 16) % 200);
    }
}

static void test_every_algo_hands_the_ctx_to_every_call() {
    static int32_t buf[SWEEP_N], other[SWEEP_N], out[2 * SWEEP_N];
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, SWEEP_N);
    const tda_Span v = tda_span_mut_to_span(s);
    const tda_SpanMut o = TDA_SPAN_FROM_DATA_MUT(int32_t, other, SWEEP_N);
    const tda_SpanMut dst = TDA_SPAN_FROM_DATA_MUT(int32_t, out, 2 * SWEEP_N);
    void *ctx = &SWEEP;
    tda_Al al = tda_al_default();
    constexpr int32_t key = 42;
    size_t at;
    sweep_calls = 0;
    sweep_wrong = 0;

    // sorts, whole and partial
    sweep_fill(buf);
    tda_span_sort(s, cmp_seen, ctx);
    TEST_ASSERT_TRUE(tda_span_is_sorted(v, cmp_seen, ctx));
    sweep_fill(buf);
    TDA_TEST_OK(tda_span_sort_stable(s, cmp_seen, ctx, al));
    sweep_fill(buf);
    tda_span_partial_sort(s, SWEEP_N / 2, cmp_seen, ctx);
    sweep_fill(buf);
    tda_span_partial_sort(s, SWEEP_N, cmp_seen, ctx);
    sweep_fill(buf);
    tda_span_nth_elem(s, SWEEP_N / 3, cmp_seen, ctx);
    sweep_fill(buf);
    tda_span_insertion_sort(tda_span_sub_mut(s, 0, 40), cmp_seen, ctx);
    static Big big[8];
    for (size_t i = 0; i < 8; ++i) {
        big[i] = (Big){.key = (int32_t) (8 - i)};
    }
    tda_span_insertion_sort(TDA_SPAN_FROM_DATA_MUT(Big, big, 8), cmp_big_seen, ctx);

    // heaps
    sweep_fill(buf);
    tda_span_make_heap(tda_span_sub_mut(s, 0, SWEEP_N - 1), cmp_seen, ctx);
    tda_span_push_heap(s, cmp_seen, ctx);
    TEST_ASSERT_TRUE(tda_span_is_heap(v, cmp_seen, ctx));
    TEST_ASSERT_EQUAL_size_t(SWEEP_N, tda_span_is_heap_until(v, cmp_seen, ctx));
    tda_span_pop_heap(s, cmp_seen, ctx);
    tda_span_sort_heap(s, cmp_seen, ctx);

    // merges, with a buffer for either run and with none
    for (size_t mid = SWEEP_N / 4; mid < SWEEP_N; mid += SWEEP_N / 2) {
        for (int with_buf = 0; with_buf < 2; ++with_buf) {
            sweep_fill(buf);
            tda_span_sort(tda_span_sub_mut(s, 0, mid), cmp_seen, ctx);
            tda_span_sort(tda_span_sub_mut(s, mid, SWEEP_N - mid), cmp_seen, ctx);
            tda_span_inplace_merge(s, mid, cmp_seen, ctx, with_buf ? al : (tda_Al){});
            TEST_ASSERT_TRUE(tda_span_is_sorted(v, cmp_seen, ctx));
        }
    }

    // searches over the sorted span
    TEST_ASSERT_TRUE(tda_span_binary_search(v, &key, cmp_seen, ctx, &at));
    const tda_Range r = tda_span_equal_range(v, &key, cmp_seen, ctx);
    TEST_ASSERT_TRUE(r.lo < r.hi);
    TEST_ASSERT_TRUE(tda_span_lower_bound(v, &key, cmp_seen, ctx) <= tda_span_upper_bound(v, &key, cmp_seen, ctx));
    TEST_ASSERT_TRUE(tda_span_partition_point(v, small_seen, ctx) > 0);
    TEST_ASSERT_TRUE(tda_span_is_partitioned(v, small_seen, ctx));

    // set ops between the sorted span and a sorted copy of every other elem
    size_t half = 0;
    for (size_t i = 0; i < SWEEP_N; i += 2) {
        other[half++] = buf[i];
    }
    const tda_Span h = TDA_SPAN_FROM_DATA(int32_t, other, half);
    TEST_ASSERT_TRUE(tda_span_includes(v, h, cmp_seen, ctx));
    tda_span_merge(tda_span_sub_mut(dst, 0, SWEEP_N + half), v, h, cmp_seen, ctx);
    TEST_ASSERT_TRUE(tda_span_set_union(dst, v, h, cmp_seen, ctx) > 0);
    TEST_ASSERT_TRUE(tda_span_set_intersection(dst, v, h, cmp_seen, ctx) > 0);
    TEST_ASSERT_TRUE(tda_span_set_difference(dst, v, h, cmp_seen, ctx) > 0);
    TEST_ASSERT_TRUE(tda_span_set_symmetric_difference(dst, v, h, cmp_seen, ctx) > 0);

    // linear searches over shuffled elems
    sweep_fill(buf);
    TEST_ASSERT_TRUE(tda_span_find(v, &buf[SWEEP_N - 1], eq_seen, ctx, &at));
    TEST_ASSERT_TRUE(tda_span_find_if(v, small_seen, ctx, &at));
    TEST_ASSERT_TRUE(tda_span_find_last(v, &buf[0], eq_seen, ctx, &at));
    TEST_ASSERT_TRUE(tda_span_find_last_if(v, small_seen, ctx, &at));
    TEST_ASSERT_TRUE(tda_span_find_sub(v, tda_span_sub(v, SWEEP_N - 3, 3), eq_seen, ctx, &at));
    TEST_ASSERT_TRUE(tda_span_find_last_sub(v, tda_span_sub(v, 0, 3), eq_seen, ctx, &at));
    TEST_ASSERT_TRUE(tda_span_find_run(v, &buf[0], 1, eq_seen, ctx, &at));
    TEST_ASSERT_TRUE(tda_span_find_any_of(v, tda_span_sub(v, SWEEP_N - 2, 2), eq_seen, ctx, &at));
    (void) tda_span_find_adjacent(v, eq_seen, ctx, &at);
    TEST_ASSERT_TRUE(tda_span_contains(v, &buf[SWEEP_N / 2], eq_seen, ctx));
    TEST_ASSERT_TRUE(tda_span_count(v, &buf[0], eq_seen, ctx) > 0);
    TEST_ASSERT_TRUE(tda_span_count_if(v, small_seen, ctx) > 0);
    TEST_ASSERT_FALSE(tda_span_all_of(v, small_seen, ctx));
    TEST_ASSERT_TRUE(tda_span_any_of(v, small_seen, ctx));
    TEST_ASSERT_FALSE(tda_span_none_of(v, small_seen, ctx));
    TEST_ASSERT_TRUE(tda_span_starts_with(v, tda_span_sub(v, 0, 5), eq_seen, ctx));
    TEST_ASSERT_TRUE(tda_span_ends_with(v, tda_span_sub(v, SWEEP_N - 5, 5), eq_seen, ctx));
    (void) tda_span_min_elem(v, cmp_seen, ctx);
    (void) tda_span_max_elem(v, cmp_seen, ctx);
    (void) tda_span_minmax_elem(v, cmp_seen, ctx);
    (void) tda_span_is_sorted_until(v, cmp_seen, ctx);

    // comparisons against a reversed copy
    for (size_t i = 0; i < SWEEP_N; ++i) {
        other[i] = buf[SWEEP_N - 1 - i];
    }
    const tda_Span rev = tda_span_mut_to_span(o);
    TEST_ASSERT_TRUE(tda_span_is_permutation(v, rev, eq_seen, ctx));
    TEST_ASSERT_TRUE(tda_span_mismatch(v, rev, eq_seen, ctx, &at));
    TEST_ASSERT_FALSE(tda_span_eq_by(v, rev, eq_seen, ctx));
    (void) tda_span_cmp(v, rev, cmp_seen, ctx);

    // folds and maps
    int64_t acc = 0;
    tda_span_fold(v, &acc, fold_seen, ctx);
    tda_span_fold_back(v, &acc, fold_seen, ctx);
    tda_span_partial_sum(o, v, add_seen, ctx);
    tda_span_adjacent_difference(o, v, add_seen, ctx);
    tda_span_transform(o, v, double_seen, ctx);
    tda_span_zip_with(o, v, rev, add_seen, ctx);
    tda_span_generate(o, gen_seen, ctx);
    TEST_ASSERT_TRUE(tda_span_copy_if(tda_span_sub_mut(dst, 0, SWEEP_N), v, small_seen, ctx) > 0);

    // in-place edits
    sweep_fill(buf);
    tda_span_replace(s, &buf[0], &key, eq_seen, ctx);
    tda_span_replace_if(s, &key, small_seen, ctx);
    sweep_fill(buf);
    TDA_TEST_OK(tda_span_partition_stable(s, small_seen, ctx, al, &at));
    sweep_fill(buf);
    (void) tda_span_partition(s, small_seen, ctx);
    sweep_fill(buf);
    (void) tda_span_remove(s, &buf[0], eq_seen, ctx);
    sweep_fill(buf);
    (void) tda_span_remove_if(s, small_seen, ctx);
    sweep_fill(buf);
    tda_span_sort(s, cmp_seen, ctx);
    (void) tda_span_unique(s, eq_seen, ctx);

    // every order of a few elems, both ways
    int32_t few[5] = {1, 2, 2, 3, 4};
    const tda_SpanMut f = TDA_SPAN_FROM_DATA_MUT(int32_t, few, 5);
    while (tda_span_next_permutation(f, cmp_seen, ctx)) {
    }
    while (tda_span_prev_permutation(f, cmp_seen, ctx)) {
    }

    TEST_ASSERT_TRUE(sweep_calls > 0);
    TEST_ASSERT_EQUAL_size_t(0, sweep_wrong);
}

// the containers that take an equality per call hand it their caller's ctx
static void test_container_lookups_hand_the_ctx_on() {
    void *ctx = &SWEEP;
    sweep_calls = 0;
    sweep_wrong = 0;

    tda_HMap a = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_al_default());
    tda_HMap b = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_al_default());
    for (int32_t i = 0; i < 10; ++i) {
        TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &a, i, i, nullptr));
        TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &b, i, i, nullptr));
    }
    TEST_ASSERT_TRUE(tda_hmap_eq_by(&a, &b, eq_seen, ctx));

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 5, 6, 7));
    TEST_ASSERT_NOT_NULL(tda_list_find(&l, &(int32_t){7}, eq_seen, ctx));

    TEST_ASSERT_TRUE(sweep_calls > 0);
    TEST_ASSERT_EQUAL_size_t(0, sweep_wrong);

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
    tda_list_deinit(&l);
}

/* ========== containers that store a callback ========== */

// the map stores one ctx for its hasher and its equality: 3, 13 and 23 are one key
static void test_hmap_hashes_and_matches_under_its_ctx() {
    int32_t ten = 10;
    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, hash_mod, eq_mod, &ten, tda_al_default());
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hmap_key_ctx(&m));

    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &m, 3, 100, nullptr));
    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &m, 13, 200, nullptr));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));
    TEST_ASSERT_EQUAL_INT32(200, *TDA_HMAP_GET_AS(int32_t, int32_t, &m, 23));

    // every other lookup hashes under it too
    TEST_ASSERT_EQUAL_INT32(200, *(int32_t *) tda_hmap_get_mut(&m, &(int32_t){33}));
    TEST_ASSERT_NOT_NULL(tda_hmap_find(&m, &(int32_t){43}));
    TEST_ASSERT_NOT_NULL(tda_hmap_find_mut(&m, &(int32_t){53}));
    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, &m, 63, 300, &node));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));

    // a copy keeps the ctx, so it still sees one key, and removing any spelling of it
    // removes that key
    tda_HMap copy = {};
    TDA_TEST_OK(tda_hmap_copy(&m, &copy));
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hmap_key_ctx(&copy));
    TEST_ASSERT_TRUE(TDA_HMAP_CONTAINS(int32_t, &copy, 33));
    TEST_ASSERT_TRUE(tda_hmap_remove(&copy, &(int32_t){43}));
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&copy));

    tda_HMap with_cap = {};
    TDA_TEST_OK(TDA_HMAP_INIT_CAP(int32_t, int32_t, 8, hash_mod, eq_mod, &ten, tda_al_default(), &with_cap));
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hmap_key_ctx(&with_cap));

    tda_HSet set = {};
    TDA_TEST_OK(TDA_HSET_INIT_CAP(int32_t, 8, hash_mod, eq_mod, &ten, tda_al_default(), &set));
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hset_key_ctx(&set));

    tda_hset_deinit(&set);
    tda_hmap_deinit(&with_cap);
    tda_hmap_deinit(&copy);
    tda_hmap_deinit(&m);
}

// the queue serves by weights it does not hold
static void test_pqueue_serves_by_weights_in_its_ctx() {
    int32_t weight[4] = {5, 50, 0, 20};
    tda_PQueue q = TDA_PQUEUE_INIT(size_t, cmp_by_key, weight, tda_al_default());
    TEST_ASSERT_EQUAL_PTR(weight, tda_pqueue_cmp_ctx(&q));

    for (size_t i = 0; i < 4; ++i) {
        TDA_TEST_OK(TDA_PQUEUE_PUSH(size_t, &q, i));
    }

    // greatest weight first
    constexpr size_t want[4] = {1, 3, 0, 2};
    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_size_t(want[i], *TDA_PQUEUE_TOP_AS(size_t, &q));
        tda_pqueue_pop(&q);
    }

    // 2 of weight 0 takes the place of 1 of weight 50, then sinks
    for (size_t i = 0; i < 4; ++i) {
        TDA_TEST_OK(TDA_PQUEUE_PUSH(size_t, &q, i));
    }
    TDA_PQUEUE_REPLACE_TOP(size_t, &q, 2);
    constexpr size_t then[4] = {3, 0, 2, 2};
    for (size_t i = 0; i < 4; ++i) {
        size_t got;
        TDA_PQUEUE_POP_TO(size_t, &q, &got);
        TEST_ASSERT_EQUAL_size_t(then[i], got);
    }

    tda_pqueue_deinit(&q);
}

// every constructor that orders what it is given orders it under the ctx
static void test_pqueue_constructors_heapify_under_the_ctx() {
    int32_t weight[4] = {5, 50, 0, 20};
    constexpr size_t idx[4] = {0, 1, 2, 3};
    tda_Al al = tda_al_default();

    tda_PQueue a = {};
    TDA_TEST_OK(TDA_PQUEUE_INIT_CAP(size_t, 4, cmp_by_key, weight, al, &a));
    TEST_ASSERT_EQUAL_PTR(weight, tda_pqueue_cmp_ctx(&a));
    for (size_t i = 0; i < 4; ++i) {
        TDA_TEST_OK(TDA_PQUEUE_PUSH(size_t, &a, i));
    }
    TEST_ASSERT_EQUAL_size_t(1, *TDA_PQUEUE_TOP_AS(size_t, &a));

    tda_PQueue b = {};
    TDA_TEST_OK(tda_pqueue_from_data(idx, 4, TDA_LAYOUT_OF(size_t), cmp_by_key, weight, al, &b));
    TEST_ASSERT_EQUAL_size_t(1, *TDA_PQUEUE_TOP_AS(size_t, &b));

    tda_PQueue c = {};
    TDA_TEST_OK(tda_pqueue_from_span(TDA_SPAN_FROM_DATA(size_t, idx, 4), cmp_by_key, weight, al, &c));
    TEST_ASSERT_EQUAL_size_t(1, *TDA_PQUEUE_TOP_AS(size_t, &c));
    tda_pqueue_pop(&c);
    TEST_ASSERT_EQUAL_size_t(3, *TDA_PQUEUE_TOP_AS(size_t, &c));

    tda_PQueue d = {};
    TDA_TEST_OK(TDA_PQUEUE_FROM_DATA(size_t, idx, 4, cmp_by_key, weight, al, &d));
    TEST_ASSERT_EQUAL_PTR(weight, tda_pqueue_cmp_ctx(&d));

    tda_PQueue e = {};
    TDA_TEST_OK(TDA_PQUEUE_OF(size_t, cmp_by_key, weight, al, &e, 0, 1, 2, 3));
    TEST_ASSERT_EQUAL_PTR(weight, tda_pqueue_cmp_ctx(&e));

    tda_pqueue_deinit(&a);
    tda_pqueue_deinit(&b);
    tda_pqueue_deinit(&c);
    tda_pqueue_deinit(&d);
    tda_pqueue_deinit(&e);
}

// built from spans, 3 and 13 are one key, the later one winning
static void test_hash_containers_from_spans_hash_under_the_ctx() {
    int32_t ten = 10;
    constexpr int32_t keys[3] = {3, 13, 4};
    constexpr int32_t vals[3] = {1, 2, 3};

    tda_HMap m = {};
    TDA_TEST_OK(tda_hmap_from_spans(
        TDA_SPAN_FROM_DATA(int32_t, keys, 3),
        TDA_SPAN_FROM_DATA(int32_t, vals, 3),
        hash_mod,
        eq_mod,
        &ten,
        tda_al_default(),
        &m
    ));
    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(&m));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_HMAP_GET_AS(int32_t, int32_t, &m, 23));

    tda_HSet s = {};
    TDA_TEST_OK(tda_hset_from_span(
        TDA_SPAN_FROM_DATA(int32_t, keys, 3),
        hash_mod,
        eq_mod,
        &ten,
        tda_al_default(),
        &s
    ));
    TEST_ASSERT_EQUAL_size_t(2, tda_hset_len(&s));

    tda_hmap_deinit(&m);
    tda_hset_deinit(&s);
}

// each side is searched with its own hasher and ctx, so sets salted apart still compare
// and combine, as long as their equalities agree
static void test_hash_containers_of_differing_seeds_meet() {
    tda_Hash seed_a = 0x1234, seed_b = 0xabcd0000;
    tda_Al al = tda_al_default();

    tda_HSet a = TDA_HSET_INIT(int32_t, hash_seeded, tda_eq_i32, &seed_a, al);
    tda_HSet b = TDA_HSET_INIT(int32_t, hash_seeded, tda_eq_i32, &seed_b, al);
    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, hash_seeded, tda_eq_i32, &seed_a, al);
    tda_HMap n = TDA_HMAP_INIT(int32_t, int32_t, hash_seeded, tda_eq_i32, &seed_b, al);

    for (int32_t i = 0; i < 40; ++i) {
        TDA_TEST_OK(TDA_HSET_INSERT(int32_t, &a, i, nullptr));
        TDA_TEST_OK(TDA_HSET_INSERT(int32_t, &b, 39 - i, nullptr));
        TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &m, i, i * 2, nullptr));
        TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &n, 39 - i, (39 - i) * 2, nullptr));
    }
    TEST_ASSERT_TRUE(tda_hset_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_hmap_eq(&m, &n));

    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, &b, 40, nullptr));
    TEST_ASSERT_FALSE(tda_hset_eq(&a, &b));
    TDA_TEST_OK(tda_hset_union(&a, &b));
    TEST_ASSERT_TRUE(tda_hset_eq(&a, &b));
    TEST_ASSERT_EQUAL_PTR(&seed_a, tda_hset_key_ctx(&a));

    tda_hset_deinit(&a);
    tda_hset_deinit(&b);
    tda_hmap_deinit(&m);
    tda_hmap_deinit(&n);
}

// every copy takes the ctx along with the hasher and the equality, every move hands it
// over, a swap trades it
static void test_hmap_and_hset_carry_their_ctx() {
    int32_t ten = 10, seven = 7;
    tda_Al al = tda_al_default();

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, hash_mod, eq_mod, &ten, al);
    tda_HMap n = TDA_HMAP_INIT(int32_t, int32_t, hash_mod, eq_mod, &seven, al);

    tda_HMap c = {};
    TDA_TEST_OK(tda_hmap_copy_with(&m, al, &c));
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hmap_key_ctx(&c));
    TDA_TEST_OK(tda_hmap_copy_assign(&c, &n));
    TEST_ASSERT_EQUAL_PTR(&seven, tda_hmap_key_ctx(&c));

    tda_hmap_swap(&m, &n);
    TEST_ASSERT_EQUAL_PTR(&seven, tda_hmap_key_ctx(&m));
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hmap_key_ctx(&n));
    tda_hmap_move_assign(&m, &n);
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hmap_key_ctx(&m));

    tda_HSet s = TDA_HSET_INIT(int32_t, hash_mod, eq_mod, &ten, al);
    tda_HSet u = TDA_HSET_INIT(int32_t, hash_mod, eq_mod, &seven, al);
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hset_key_ctx(&s));
    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, &s, 3, nullptr));
    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, &s, 13, nullptr));
    TEST_ASSERT_EQUAL_size_t(1, tda_hset_len(&s));

    tda_HSet sc = {};
    TDA_TEST_OK(tda_hset_copy(&s, &sc));
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hset_key_ctx(&sc));
    tda_hset_swap(&sc, &u);
    TEST_ASSERT_EQUAL_PTR(&seven, tda_hset_key_ctx(&sc));
    tda_hset_move_assign(&sc, &u);
    TEST_ASSERT_EQUAL_PTR(&ten, tda_hset_key_ctx(&sc));

    tda_hmap_deinit(&c);
    tda_hmap_deinit(&m);
    tda_hmap_deinit(&n);
    tda_hset_deinit(&s);
    tda_hset_deinit(&u);
    tda_hset_deinit(&sc);
}

// a move hands the comparator over with its ctx, and a swap trades both
static void test_pqueue_ctx_travels_with_its_comparator() {
    int32_t a_ctx[1] = {0}, b_ctx[1] = {0};
    tda_PQueue a = TDA_PQUEUE_INIT(size_t, cmp_by_key, a_ctx, tda_al_default());
    tda_PQueue b = TDA_PQUEUE_INIT(size_t, cmp_by_key, b_ctx, tda_al_default());

    tda_pqueue_swap(&a, &b);
    TEST_ASSERT_EQUAL_PTR(b_ctx, tda_pqueue_cmp_ctx(&a));
    TEST_ASSERT_EQUAL_PTR(a_ctx, tda_pqueue_cmp_ctx(&b));

    tda_pqueue_move_assign(&a, &b);
    TEST_ASSERT_EQUAL_PTR(a_ctx, tda_pqueue_cmp_ctx(&a));

    // copies take the ctx of their source
    tda_PQueue c = {};
    TDA_TEST_OK(tda_pqueue_copy(&a, &c));
    TEST_ASSERT_EQUAL_PTR(a_ctx, tda_pqueue_cmp_ctx(&c));

    int32_t d_ctx[1] = {0};
    tda_PQueue d = TDA_PQUEUE_INIT(size_t, cmp_by_key, d_ctx, tda_al_default());
    tda_PQueue e = {};
    TDA_TEST_OK(tda_pqueue_copy_with(&d, tda_al_default(), &e));
    TEST_ASSERT_EQUAL_PTR(d_ctx, tda_pqueue_cmp_ctx(&e));

    TDA_TEST_OK(tda_pqueue_copy_assign(&c, &d));
    TEST_ASSERT_EQUAL_PTR(d_ctx, tda_pqueue_cmp_ctx(&c));

    tda_pqueue_deinit(&c);
    tda_pqueue_deinit(&d);
    tda_pqueue_deinit(&e);

    tda_pqueue_deinit(&a);
    tda_pqueue_deinit(&b);
}

static void test_list_sorts_and_finds_under_the_ctx() {
    int32_t keys[4] = {9, 7, 8, 6};
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(size_t, tda_al_default(), &l, 0, 1, 2, 3));

    tda_list_sort(&l, cmp_by_key, keys);

    constexpr size_t want[4] = {3, 1, 2, 0};
    size_t i = 0;
    TDA_LIST_FOR_EACH (node, &l) {
        TEST_ASSERT_EQUAL_size_t(want[i++], *TDA_LIST_NODE_ELEM_AS(size_t, &l, node));
    }

    // an equality with a ctx finds through the macro as through the function
    int32_t five = 5;
    tda_List n = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &n, 1, 2, 13));
    const tda_ListNode *hit = TDA_LIST_FIND(int32_t, &n, 8, eq_mod, &five);
    TEST_ASSERT_NOT_NULL(hit);
    TEST_ASSERT_EQUAL_INT32(13, *TDA_LIST_NODE_ELEM_AS(int32_t, &n, hit));
    TEST_ASSERT_EQUAL_PTR(hit, tda_list_find_mut(&n, &(int32_t){18}, eq_mod, &five));
    TEST_ASSERT_EQUAL_PTR(hit, TDA_LIST_FIND_MUT(int32_t, &n, 3, eq_mod, &five));

    tda_list_deinit(&n);
    tda_list_deinit(&l);
}

static void test_list_merges_under_the_ctx() {
    tda_List a = {}, b = {};
    TDA_TEST_OK(TDA_LIST_OF(size_t, tda_al_default(), &a, 3, 6, 0, 4)); // keys 10 30 70 80
    TDA_TEST_OK(TDA_LIST_OF(size_t, tda_al_default(), &b, 1, 5, 2, 7)); // keys 20 40 50 60

    TDA_TEST_OK(tda_list_merge(&a, &b, cmp_by_key, KEYS));

    size_t i = 0;
    TDA_LIST_FOR_EACH (node, &a) {
        TEST_ASSERT_EQUAL_size_t(i++, key_rank_of(*TDA_LIST_NODE_ELEM_AS(size_t, &a, node)));
    }
    TEST_ASSERT_EQUAL_size_t(8, i);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_sort_orders_indices_by_keys_in_the_ctx);
    RUN_TEST(test_sort_stable_and_binary_search_take_it_too);
    RUN_TEST(test_find_matches_under_the_ctx);
    RUN_TEST(test_the_ordering_algos_read_the_ctx);
    RUN_TEST(test_the_merging_algos_read_the_ctx);
    RUN_TEST(test_the_searching_algos_read_the_ctx);
    RUN_TEST(test_unique_and_permutations_read_the_ctx);
    RUN_TEST(test_every_eq_by_passes_the_ctx);
    RUN_TEST(test_remove_and_replace_match_under_the_ctx);
    RUN_TEST(test_every_algo_hands_the_ctx_to_every_call);
    RUN_TEST(test_container_lookups_hand_the_ctx_on);

    RUN_TEST(test_hmap_hashes_and_matches_under_its_ctx);
    RUN_TEST(test_hash_containers_from_spans_hash_under_the_ctx);
    RUN_TEST(test_hash_containers_of_differing_seeds_meet);
    RUN_TEST(test_hmap_and_hset_carry_their_ctx);
    RUN_TEST(test_pqueue_serves_by_weights_in_its_ctx);
    RUN_TEST(test_pqueue_constructors_heapify_under_the_ctx);
    RUN_TEST(test_pqueue_ctx_travels_with_its_comparator);
    RUN_TEST(test_list_sorts_and_finds_under_the_ctx);
    RUN_TEST(test_list_merges_under_the_ctx);

    return UNITY_END();
}
