#include "tda/ds/arr.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/print.h"
#include "tda/core/util.h"

#include "support/arena.h"
#include "support/pair.h"
#include "support/probe.h"
#include "support/status.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// int32_t array holding 0, 1, ... len-1
static tda_Arr *make_arr(size_t len) {
    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_NEW_LEN(int32_t, len, tda_al_default(), &a));

    for (size_t i = 0; i < len; ++i) {
        TDA_ARR_SET(int32_t, a, i, (int32_t) i);
    }
    return a;
}

/* ========== lifetime ========== */

static void test_new_sets_shape_and_zeroes() {
    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_NEW_LEN(int32_t, 4, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_size(a));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_arr_al(a));

    constexpr int32_t zeroes[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(zeroes, tda_arr_data(a), 4);

    tda_arr_drop(a);
}

static void test_new_empty_has_no_buffer() {
    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_NEW_LEN(int32_t, 0, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(a));
    TEST_ASSERT_NULL(tda_arr_data(a));

    tda_arr_drop(a);
}

static void test_drop_null_is_noop() {
    tda_arr_drop(nullptr);
}

/* ========== from_data ========== */

static void test_from_data_copies_the_source() {
    constexpr int32_t src[4] = {5, 6, 7, 8};

    tda_Arr *a = nullptr;
    TDA_TEST_OK(tda_arr_from_data(src, 4, sizeof(int32_t), tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_size(a));
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_arr_data(a), 4);
    TEST_ASSERT_TRUE((const void *) src != tda_arr_data(a));

    tda_arr_drop(a);
}

// the array owns a copy, it does not view the source
static void test_from_data_is_detached_from_the_source() {
    int32_t src[3] = {1, 2, 3};

    tda_Arr *a = nullptr;
    TDA_TEST_OK(tda_arr_from_data(src, 3, sizeof(int32_t), tda_al_default(), &a));

    src[0] = 999;
    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, a, 0));

    tda_arr_drop(a);
}

// null source is legal while len == 0 — same rule as tda_span_from_data
static void test_from_data_empty_has_no_buffer() {
    tda_Arr *a = nullptr;
    TDA_TEST_OK(tda_arr_from_data(nullptr, 0, sizeof(int32_t), tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(a));
    TEST_ASSERT_NULL(tda_arr_data(a));

    tda_arr_drop(a);
}

// elem_size drives the copy, so a type wider than a word must arrive whole
static void test_from_data_copies_whole_elements() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Arr *arr = nullptr;
    TDA_TEST_OK(tda_arr_from_data(src, 2, sizeof(Pair), tda_al_default(), &arr));

    const Pair *got = tda_arr_data(arr);
    TEST_ASSERT_EQUAL_INT64(1, got[0].a);
    TEST_ASSERT_EQUAL_INT64(2, got[0].b);
    TEST_ASSERT_EQUAL_INT64(3, got[1].a);
    TEST_ASSERT_EQUAL_INT64(4, got[1].b);

    tda_arr_drop(arr);
}

/* ========== access ========== */

static void test_set_get_roundtrip() {
    tda_Arr *a = make_arr(5);

    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT32((int32_t) i, *TDA_ARR_GET_AS(int32_t, a, i));
    }

    tda_arr_drop(a);
}

static void test_get_mut_writes_through() {
    tda_Arr *a = make_arr(3);

    *TDA_ARR_GET_MUT_AS(int32_t, a, 1) = 42;
    TEST_ASSERT_EQUAL_INT32(42, *TDA_ARR_GET_AS(int32_t, a, 1));

    tda_arr_drop(a);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_Arr *src = make_arr(4);

    tda_Arr *dst = nullptr;
    TDA_TEST_OK(tda_arr_copy(src, &dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(src), tda_arr_data(dst), 4);
    TEST_ASSERT_TRUE(tda_arr_data(src) != tda_arr_data(dst));

    TDA_ARR_SET(int32_t, src, 0, 999);
    TEST_ASSERT_EQUAL_INT32(0, *TDA_ARR_GET_AS(int32_t, dst, 0));

    tda_arr_drop(dst);
    tda_arr_drop(src);
}

static void test_copy_assign_grow_shrink_empty() {
    tda_Arr *src = make_arr(6);
    tda_Arr *dst = make_arr(2);

    // grow: 2 -> 6
    TDA_TEST_OK(tda_arr_copy_assign(src, dst));
    TEST_ASSERT_EQUAL_size_t(6, tda_arr_len(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(src), tda_arr_data(dst), 6);

    // shrink: 6 -> 3
    tda_Arr *small = make_arr(3);
    TDA_TEST_OK(tda_arr_copy_assign(small, dst));
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(small), tda_arr_data(dst), 3);

    // shrink to empty: buffer must be released, not kept
    tda_Arr *empty = make_arr(0);
    TDA_TEST_OK(tda_arr_copy_assign(empty, dst));
    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(dst));
    TEST_ASSERT_NULL(tda_arr_data(dst));

    tda_arr_drop(empty);
    tda_arr_drop(small);
    tda_arr_drop(dst);
    tda_arr_drop(src);
}

static void test_copy_assign_self_is_noop() {
    tda_Arr *a = make_arr(3);

    TDA_TEST_OK(tda_arr_copy_assign(a, a));
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(a));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, a, 2));

    tda_arr_drop(a);
}

/* ========== mods / views ========== */

static void test_swap_exchanges_contents() {
    tda_Arr *a = make_arr(2);
    tda_Arr *b = make_arr(5);

    tda_arr_swap(a, b);

    TEST_ASSERT_EQUAL_size_t(5, tda_arr_len(a));
    TEST_ASSERT_EQUAL_size_t(2, tda_arr_len(b));
    TEST_ASSERT_EQUAL_INT32(4, *TDA_ARR_GET_AS(int32_t, a, 4));

    tda_arr_drop(b);
    tda_arr_drop(a);
}

static void test_span_views_the_same_memory() {
    tda_Arr *a = make_arr(4);

    const tda_SpanMut s = tda_arr_to_span_mut(a);
    TEST_ASSERT_EQUAL_PTR(tda_arr_data(a), s.data);
    TEST_ASSERT_EQUAL_size_t(4, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem_size);

    TDA_SPAN_SET(int32_t, s, 0, 77);
    TEST_ASSERT_EQUAL_INT32(77, *TDA_ARR_GET_AS(int32_t, a, 0));

    tda_arr_drop(a);
}

/* ========== from_span ========== */

static void test_from_span_copies_the_view() {
    constexpr int32_t src[3] = {7, 8, 9};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, src, 3);

    tda_Arr *a = nullptr;
    TDA_TEST_OK(tda_arr_from_span(s, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_size(a));
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_arr_data(a), 3);
    TEST_ASSERT_TRUE((const void *) src != tda_arr_data(a));

    tda_arr_drop(a);
}

static void test_from_span_empty_has_no_buffer() {
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, nullptr, 0);

    tda_Arr *a = nullptr;
    TDA_TEST_OK(tda_arr_from_span(s, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_size(a));
    TEST_ASSERT_NULL(tda_arr_data(a));

    tda_arr_drop(a);
}

// arr -> span -> arr must round-trip without touching the original
static void test_from_span_of_an_arr_round_trips() {
    tda_Arr *src = make_arr(4);

    tda_Arr *dst = nullptr;
    TDA_TEST_OK(tda_arr_from_span(tda_arr_to_span(src), tda_al_default(), &dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(src), tda_arr_data(dst), 4);
    TEST_ASSERT_TRUE(tda_arr_data(src) != tda_arr_data(dst));

    tda_arr_drop(dst);
    tda_arr_drop(src);
}

/* ========== first / last ========== */

static void test_first_and_last_address_the_ends() {
    tda_Arr *a = make_arr(4);

    TEST_ASSERT_EQUAL_INT32(0, *TDA_ARR_FRONT_AS(int32_t, a));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_BACK_AS(int32_t, a));
    TEST_ASSERT_EQUAL_PTR(tda_arr_data(a), tda_arr_front(a));
    TEST_ASSERT_EQUAL_PTR(TDA_ARR_GET_AS(int32_t, a, 3), tda_arr_back(a));

    tda_arr_drop(a);
}

static void test_first_and_last_mut_write_through() {
    tda_Arr *a = make_arr(4);

    *TDA_ARR_FRONT_MUT_AS(int32_t, a) = 10;
    *TDA_ARR_BACK_MUT_AS(int32_t, a) = 20;

    TEST_ASSERT_EQUAL_INT32(10, *TDA_ARR_GET_AS(int32_t, a, 0));
    TEST_ASSERT_EQUAL_INT32(20, *TDA_ARR_GET_AS(int32_t, a, 3));

    tda_arr_drop(a);
}

static void test_first_and_last_coincide_on_a_single_elem() {
    tda_Arr *a = make_arr(1);

    TEST_ASSERT_EQUAL_PTR(tda_arr_front(a), tda_arr_back(a));
    TEST_ASSERT_EQUAL_PTR(tda_arr_front_mut(a), tda_arr_back_mut(a));

    tda_arr_drop(a);
}

/* ========== data_mut / foreach ========== */

static void test_data_mut_writes_through() {
    tda_Arr *a = make_arr(3);

    int32_t *d = tda_arr_data_mut(a);
    d[2] = 99;

    TEST_ASSERT_EQUAL_INT32(99, *TDA_ARR_GET_AS(int32_t, a, 2));

    tda_arr_drop(a);
}

/* ========== swap_elems ========== */

static void test_swap_elems_exchanges_the_pair() {
    tda_Arr *a = make_arr(4);

    tda_arr_swap_elems(a, 0, 3);

    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, a, 0));
    TEST_ASSERT_EQUAL_INT32(0, *TDA_ARR_GET_AS(int32_t, a, 3));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, a, 1));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, a, 2));

    tda_arr_drop(a);
}

static void test_swap_elems_same_index_is_noop() {
    tda_Arr *a = make_arr(3);

    tda_arr_swap_elems(a, 1, 1);

    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, a, 1));

    tda_arr_drop(a);
}

// elem_size drives the swap, so a type wider than a word must move whole
static void test_swap_elems_moves_wide_elems_whole() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Arr *arr = nullptr;
    TDA_TEST_OK(TDA_ARR_FROM_DATA(Pair, src, 2, tda_al_default(), &arr));

    tda_arr_swap_elems(arr, 0, 1);

    TEST_ASSERT_EQUAL_INT64(3, TDA_ARR_GET_AS(Pair, arr, 0)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_ARR_GET_AS(Pair, arr, 0)->b);
    TEST_ASSERT_EQUAL_INT64(1, TDA_ARR_GET_AS(Pair, arr, 1)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_ARR_GET_AS(Pair, arr, 1)->b);

    tda_arr_drop(arr);
}

/* ========== swap ========== */

static void test_swap_self_is_noop() {
    tda_Arr *a = make_arr(3);
    const void *before = tda_arr_data(a);

    tda_arr_swap(a, a);

    TEST_ASSERT_EQUAL_PTR(before, tda_arr_data(a));
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(a));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, a, 2));

    tda_arr_drop(a);
}

// one allocator on both sides: the buffers are handed over, never copied
static void test_swap_same_allocator_hands_over_buffers() {
    tda_Arr *a = make_arr(2);
    tda_Arr *b = make_arr(5);

    const void *pa = tda_arr_data(a);
    const void *pb = tda_arr_data(b);

    tda_arr_swap(a, b);

    TEST_ASSERT_EQUAL_PTR(pb, tda_arr_data(a));
    TEST_ASSERT_EQUAL_PTR(pa, tda_arr_data(b));

    tda_arr_drop(b);
    tda_arr_drop(a);
}

/* ========== to span ========== */

static void test_to_span_matches_the_arr_shape() {
    tda_Arr *a = make_arr(4);

    const tda_Span s = tda_arr_to_span(a);

    TEST_ASSERT_EQUAL_PTR(tda_arr_data(a), s.data);
    TEST_ASSERT_EQUAL_size_t(4, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem_size);
    TEST_ASSERT_EQUAL_INT32(2, *TDA_SPAN_GET_AS(int32_t, s, 2));

    tda_arr_drop(a);
}

static void test_to_span_of_empty_keeps_elem_size() {
    tda_Arr *a = make_arr(0);

    const tda_Span s = tda_arr_to_span(a);

    TEST_ASSERT_NULL(s.data);
    TEST_ASSERT_EQUAL_size_t(0, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem_size);

    tda_arr_drop(a);
}

/* ========== allocators ========== */

static void test_copy_inherits_the_source_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *src = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, arena, &src, 1, 2, 3));

    tda_Arr *dst = nullptr;
    TDA_TEST_OK(tda_arr_copy(src, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_arr_al(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(src), tda_arr_data(dst), 3);

    tda_arr_drop(dst);
    tda_arr_drop(src);
    tda_al_arena_drop(arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *src = make_arr(4);

    tda_Arr *dst = nullptr;
    TDA_TEST_OK(tda_arr_copy_with(src, arena, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_arr_al(dst));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_arr_al(src));
    TEST_ASSERT_TRUE(tda_arr_eq(src, dst));

    // the source is gone and the copy still holds the elems: they were taken, not viewed
    tda_arr_drop(src);
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, dst, 3));

    tda_arr_drop(dst);
    tda_al_arena_drop(arena);
}

// the blocks are asked of the allocator the copy is going to, not of the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    tda_Arr *src = make_arr(4);

    tda_Arr *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_copy_with(src, arena, &dst));
    TEST_ASSERT_NULL(dst);
    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(src));

    tda_arr_drop(src);
    tda_al_arena_drop(arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr *src = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, &al, &src, 1, 2, 3));

    tda_Arr *dst = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, &al, &dst, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_arr_move_assign(src, dst));

    // nothing was asked of the allocator: the block changed hands as it stood
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(dst));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, dst, 1));

    // the source is left empty and usable, not dangling
    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(src));
    TEST_ASSERT_NULL(tda_arr_data(src));

    tda_arr_drop(src);
    tda_arr_drop(dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_move_assign_across_allocators_empties_the_source() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *src = make_arr(4);

    tda_Arr *dst = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, arena, &dst, 9));

    TDA_TEST_OK(tda_arr_move_assign(src, dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(dst));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, dst, 3));
    TEST_ASSERT_EQUAL_PTR(arena, tda_arr_al(dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(src));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_arr_al(src));

    tda_arr_drop(src);
    tda_arr_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_across_allocators_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *dst = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, arena, &dst, 9));
    tda_test_arena_leave(arena, 0);

    tda_Arr *src = make_arr(4);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_move_assign(src, dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(src));
    TEST_ASSERT_EQUAL_size_t(1, tda_arr_len(dst));
    TEST_ASSERT_EQUAL_INT32(9, *TDA_ARR_GET_AS(int32_t, dst, 0));

    tda_arr_drop(src);
    tda_arr_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_Arr *a = make_arr(3);

    TDA_TEST_OK(tda_arr_move_assign(a, a));

    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(a));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, a, 2));

    tda_arr_drop(a);
}

// assignment resizes through the target's allocator, not the source's
static void test_copy_assign_keeps_the_target_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *src = make_arr(4);

    tda_Arr *dst = nullptr;
    TDA_TEST_OK(TDA_ARR_NEW_LEN(int32_t, 1, arena, &dst));

    TDA_TEST_OK(tda_arr_copy_assign(src, dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(dst));
    TEST_ASSERT_EQUAL_PTR(arena, tda_arr_al(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(src), tda_arr_data(dst), 4);

    tda_arr_drop(dst);
    tda_arr_drop(src);
    tda_al_arena_drop(arena);
}

/* ========== allocation failure ========== */

// len * elem_size overflows size_t: reported, never attempted
static void test_new_len_reports_size_overflow() {
    tda_Arr *a = nullptr;

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_new_len(SIZE_MAX, 2, tda_al_default(), &a));

    TEST_ASSERT_NULL(a); // out is untouched on failure
}

static void test_from_data_reports_size_overflow() {
    constexpr int32_t src[1] = {1};
    tda_Arr *a = nullptr;

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_from_data(src, SIZE_MAX, 2, tda_al_default(), &a));

    TEST_ASSERT_NULL(a);
}

static void test_new_len_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 64);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *a = nullptr;

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_ARR_NEW_LEN(int32_t, 1000, arena, &a));

    TEST_ASSERT_NULL(a);

    tda_al_arena_drop(arena);
}

static void test_from_data_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 128);
    TEST_ASSERT_NOT_NULL(arena);

    constexpr int32_t src[4] = {1, 2, 3, 4};
    tda_Arr *a = nullptr;

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_from_data(src, 1000, sizeof(int32_t), arena, &a));

    TEST_ASSERT_NULL(a);

    tda_al_arena_drop(arena);
}

// a copy asks the SOURCE's allocator for both blocks, so an exhausted arena under the
// source is what refuses it
static void test_copy_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *src = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, arena, &src, 1, 2, 3));
    tda_test_arena_leave(arena, 0);

    tda_Arr *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_copy(src, &dst));

    TEST_ASSERT_NULL(dst);
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(src)); // the source is only read

    tda_al_arena_drop(arena);
}

// the header alone is refused: the buffer is never asked for, and 'out' stays untouched
static void test_copy_of_empty_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 128);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *src = nullptr;
    TDA_TEST_OK(TDA_ARR_NEW_LEN(int32_t, 0, arena, &src));
    tda_test_arena_leave(arena, 0);

    tda_Arr *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_copy(src, &dst));

    TEST_ASSERT_NULL(dst);

    tda_al_arena_drop(arena);
}

// A copy_assign that changes the length has to resize the TARGET's buffer, which is the
// only allocation this operation makes. When it is refused the target must be left whole
// — the old length, the old block and the old elems — rather than half converted
static void test_copy_assign_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Arr *other = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, arena, &other, 7, 8));

    tda_Arr *self = make_arr(8); // default allocator, 0 .. 7

    const void *before = tda_arr_data(other);
    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_copy_assign(self, other));

    TEST_ASSERT_EQUAL_size_t(2, tda_arr_len(other));
    TEST_ASSERT_EQUAL_PTR(before, tda_arr_data(other));
    TEST_ASSERT_EQUAL_INT32(7, *TDA_ARR_GET_AS(int32_t, other, 0));
    TEST_ASSERT_EQUAL_INT32(8, *TDA_ARR_GET_AS(int32_t, other, 1));

    tda_arr_drop(self);
    tda_al_arena_drop(arena);
}

// equal lengths need no new room, so the elems are written over the block the target
// already has. The probe is what makes "no allocation" checkable at all
static void test_copy_assign_of_the_same_length_never_allocates() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr *self = nullptr;
    tda_Arr *other = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, &al, &self, 1, 2, 3));
    TDA_TEST_OK(TDA_ARR_OF(int32_t, &al, &other, 9, 9, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    const void *before = tda_arr_data(other);

    TDA_TEST_OK(tda_arr_copy_assign(self, other));

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_PTR(before, tda_arr_data(other));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, other, 0));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, other, 2));

    tda_arr_drop(self);
    tda_arr_drop(other);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// The arr is built in two allocations, the header first and then the buffer. When the
// second is refused the first must not be stranded: the probe counts what is still live,
// and an arena would hide the leak because it frees everything at once
static void test_a_refused_buffer_frees_the_header() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 1);

    tda_Arr *a = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_ARR_NEW_LEN(int32_t, 4, &al, &a));

    TEST_ASSERT_NULL(a);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the same for the filled constructor, which takes its buffer with tda_alloc rather than
// tda_calloc — a different call, the same rule
static void test_a_refused_buffer_frees_the_header_of_from_data() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 1);

    tda_Arr *a = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_ARR_OF(int32_t, &al, &a, 1, 2, 3));

    TEST_ASSERT_NULL(a);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// two blocks go into a filled arr and drop must hand back both. The default allocator
// would say nothing about it, so the count comes from a probe
static void test_drop_hands_back_everything_it_took() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, &al, &a, 1, 2, 3, 4));
    TEST_ASSERT_EQUAL_size_t(2, probe.live);

    tda_arr_drop(a);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
    TEST_ASSERT_EQUAL_size_t(2, probe.dealloc_calls);
}

// an empty arr owns a header and nothing else, so drop hands back exactly one block
static void test_drop_of_empty_hands_back_the_header_alone() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_NEW_LEN(int32_t, 0, &al, &a));
    TEST_ASSERT_EQUAL_size_t(1, probe.live);

    tda_arr_drop(a);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
}

/* ========== macros ========== */

static void test_macro_of_builds_from_literals() {
    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_al_default(), &a, 4, 5, 6));

    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_size(a));

    constexpr int32_t want[3] = {4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_arr_data(a), 3);

    tda_arr_drop(a);
}

static void test_macro_of_derives_len_from_the_list() {
    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int64_t, tda_al_default(), &a, 1, 2, 3, 4, 5));

    TEST_ASSERT_EQUAL_size_t(5, tda_arr_len(a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int64_t), tda_arr_elem_size(a));

    tda_arr_drop(a);
}

static void test_macro_from_data_infers_elem_size() {
    constexpr int32_t src[2] = {1, 2};

    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_FROM_DATA(int32_t, src, 2, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(2, tda_arr_len(a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_size(a));
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_arr_data(a), 2);

    tda_arr_drop(a);
}

/* ========== bytes ========== */

static void test_bytes_is_len_times_elem_size() {
    tda_Arr *a = make_arr(4);

    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), tda_arr_bytes(a));

    tda_arr_drop(a);
}

static void test_bytes_of_empty_is_zero() {
    tda_Arr *a = make_arr(0);

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_bytes(a));

    tda_arr_drop(a);
}

// elem_size, not the elem count, drives the total
static void test_bytes_tracks_elem_size() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_FROM_DATA(Pair, src, 2, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(2 * sizeof(Pair), tda_arr_bytes(a));

    tda_arr_drop(a);
}

// the arr and its view must agree on the size of the same memory
static void test_bytes_agrees_with_the_span() {
    tda_Arr *a = make_arr(3);

    TEST_ASSERT_EQUAL_size_t(tda_span_bytes(tda_arr_to_span(a)), tda_arr_bytes(a));

    tda_arr_drop(a);
}

/* ========== compare ========== */

static void test_eq_matches_the_same_elems() {
    tda_Arr *a = make_arr(4);
    tda_Arr *b = make_arr(4);

    TEST_ASSERT_TRUE(tda_arr_eq(a, a));
    TEST_ASSERT_TRUE(tda_arr_eq(a, b));
    TEST_ASSERT_TRUE(tda_arr_eq(b, a));
    TEST_ASSERT_TRUE(tda_arr_eq_by(a, b, tda_eq_i32));

    tda_arr_drop(a);
    tda_arr_drop(b);
}

static void test_eq_parts_one_differing_elem() {
    tda_Arr *a = make_arr(4);
    tda_Arr *b = make_arr(4);
    TDA_ARR_SET(int32_t, b, 3, 99);

    TEST_ASSERT_FALSE(tda_arr_eq(a, b));
    TEST_ASSERT_FALSE(tda_arr_eq(b, a));
    TEST_ASSERT_FALSE(tda_arr_eq_by(a, b, tda_eq_i32));

    tda_arr_drop(a);
    tda_arr_drop(b);
}

// a prefix of the other, so nothing but the length tells the two apart
static void test_eq_parts_different_lengths() {
    tda_Arr *a = make_arr(4);
    tda_Arr *shorter = make_arr(3);

    TEST_ASSERT_FALSE(tda_arr_eq(a, shorter));
    TEST_ASSERT_FALSE(tda_arr_eq(shorter, a));
    TEST_ASSERT_FALSE(tda_arr_eq_by(a, shorter, tda_eq_i32));

    tda_arr_drop(a);
    tda_arr_drop(shorter);
}

static void test_eq_of_two_empties() {
    tda_Arr *a = make_arr(0);
    tda_Arr *b = make_arr(0);
    tda_Arr *one = make_arr(1);

    TEST_ASSERT_TRUE(tda_arr_eq(a, b));
    TEST_ASSERT_TRUE(tda_arr_eq_by(a, b, tda_eq_i32));
    TEST_ASSERT_FALSE(tda_arr_eq(a, one));
    TEST_ASSERT_FALSE(tda_arr_eq(one, a));

    tda_arr_drop(a);
    tda_arr_drop(b);
    tda_arr_drop(one);
}

// the equality decides, and it can see less than the bytes do: these Pairs agree in the
// first field and differ in the second
static void test_eq_by_asks_the_equality() {
    constexpr Pair lhs[2] = {{1, 10}, {2, 20}};
    constexpr Pair rhs[2] = {{1, 70}, {2, 80}};

    tda_Arr *a = nullptr;
    tda_Arr *b = nullptr;
    TDA_TEST_OK(TDA_ARR_FROM_DATA(Pair, lhs, 2, tda_al_default(), &a));
    TDA_TEST_OK(TDA_ARR_FROM_DATA(Pair, rhs, 2, tda_al_default(), &b));

    TEST_ASSERT_FALSE(tda_arr_eq(a, b));
    TEST_ASSERT_TRUE(tda_arr_eq_by(a, b, tda_test_pair_eq_a));

    tda_arr_drop(a);
    tda_arr_drop(b);
}

/* ========== print ========== */

// a printer writes to a stream, so a case reads one back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_Arr *a) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_arr_fprint(a, stream, tda_fprint_i32);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof buf - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_elems() {
    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_al_default(), &a, 5, 3, 1));

    assert_prints("[5, 3, 1]\n", a);

    tda_arr_drop(a);
}

static void test_fprint_of_a_single_elem_has_no_separator() {
    tda_Arr *a = nullptr;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_al_default(), &a, 7));

    assert_prints("[7]\n", a);

    tda_arr_drop(a);
}

static void test_fprint_of_an_empty_arr() {
    tda_Arr *a = make_arr(0);

    assert_prints("[]\n", a);

    tda_arr_drop(a);
}

// the stdout twin takes no stream and cannot be captured portably, so a case can only
// say that it runs and reaches the same printer
static void test_print_writes_to_stdout() {
    tda_Arr *a = make_arr(3);

    tda_arr_print(a, tda_fprint_i32);

    tda_arr_drop(a);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_sets_shape_and_zeroes);
    RUN_TEST(test_new_empty_has_no_buffer);
    RUN_TEST(test_drop_null_is_noop);

    RUN_TEST(test_from_data_copies_the_source);
    RUN_TEST(test_from_data_is_detached_from_the_source);
    RUN_TEST(test_from_data_empty_has_no_buffer);
    RUN_TEST(test_from_data_copies_whole_elements);

    RUN_TEST(test_set_get_roundtrip);
    RUN_TEST(test_get_mut_writes_through);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_assign_grow_shrink_empty);
    RUN_TEST(test_copy_assign_self_is_noop);

    RUN_TEST(test_swap_exchanges_contents);
    RUN_TEST(test_span_views_the_same_memory);

    RUN_TEST(test_from_span_copies_the_view);
    RUN_TEST(test_from_span_empty_has_no_buffer);
    RUN_TEST(test_from_span_of_an_arr_round_trips);

    RUN_TEST(test_first_and_last_address_the_ends);
    RUN_TEST(test_first_and_last_mut_write_through);
    RUN_TEST(test_first_and_last_coincide_on_a_single_elem);

    RUN_TEST(test_data_mut_writes_through);

    RUN_TEST(test_swap_elems_exchanges_the_pair);
    RUN_TEST(test_swap_elems_same_index_is_noop);
    RUN_TEST(test_swap_elems_moves_wide_elems_whole);

    RUN_TEST(test_swap_self_is_noop);
    RUN_TEST(test_swap_same_allocator_hands_over_buffers);

    RUN_TEST(test_to_span_matches_the_arr_shape);
    RUN_TEST(test_to_span_of_empty_keeps_elem_size);

    RUN_TEST(test_copy_inherits_the_source_allocator);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_empties_the_source);
    RUN_TEST(test_move_assign_across_allocators_reports_an_exhausted_arena);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_assign_keeps_the_target_allocator);

    RUN_TEST(test_new_len_reports_size_overflow);
    RUN_TEST(test_from_data_reports_size_overflow);
    RUN_TEST(test_new_len_reports_an_exhausted_arena);
    RUN_TEST(test_from_data_reports_an_exhausted_arena);
    RUN_TEST(test_copy_reports_an_exhausted_arena);
    RUN_TEST(test_copy_of_empty_reports_an_exhausted_arena);
    RUN_TEST(test_copy_assign_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_copy_assign_of_the_same_length_never_allocates);
    RUN_TEST(test_a_refused_buffer_frees_the_header);
    RUN_TEST(test_a_refused_buffer_frees_the_header_of_from_data);
    RUN_TEST(test_drop_hands_back_everything_it_took);
    RUN_TEST(test_drop_of_empty_hands_back_the_header_alone);

    RUN_TEST(test_macro_of_builds_from_literals);
    RUN_TEST(test_macro_of_derives_len_from_the_list);
    RUN_TEST(test_macro_from_data_infers_elem_size);

    RUN_TEST(test_bytes_is_len_times_elem_size);
    RUN_TEST(test_bytes_of_empty_is_zero);
    RUN_TEST(test_bytes_tracks_elem_size);
    RUN_TEST(test_bytes_agrees_with_the_span);


    RUN_TEST(test_eq_matches_the_same_elems);
    RUN_TEST(test_eq_parts_one_differing_elem);
    RUN_TEST(test_eq_parts_different_lengths);
    RUN_TEST(test_eq_of_two_empties);
    RUN_TEST(test_eq_by_asks_the_equality);

    RUN_TEST(test_fprint_writes_the_elems);
    RUN_TEST(test_fprint_of_a_single_elem_has_no_separator);
    RUN_TEST(test_fprint_of_an_empty_arr);
    RUN_TEST(test_print_writes_to_stdout);

    return UNITY_END();
}
