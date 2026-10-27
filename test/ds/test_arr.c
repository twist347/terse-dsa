#include "tda/ds/arr.h"
#include "tda/ds/vec.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/print.h"

#include "support/arena.h"
#include "support/pair.h"
#include "support/probe.h"
#include "support/status.h"

#include "support/print.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// int32_t array holding 0, 1, ... len-1
static tda_Arr make_arr(size_t len) {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_INIT_LEN(int32_t, len, tda_al_default(), &a));

    for (size_t i = 0; i < len; ++i) {
        TDA_ARR_SET(int32_t, &a, i, (int32_t) i);
    }

    return a;
}

/* ========== lifetime ========== */

static void test_init_sets_shape_and_zeroes() {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_INIT_LEN(int32_t, 4, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_layout(&a).size);
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_arr_al(&a)));

    constexpr int32_t zeroes[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(zeroes, tda_arr_data(&a), 4);

    tda_arr_deinit(&a);
}

static void test_init_empty_has_no_buffer() {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_INIT_LEN(int32_t, 0, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&a));
    TEST_ASSERT_NULL(tda_arr_data(&a));

    tda_arr_deinit(&a);
}

static void test_deinit_of_an_inert_value_is_noop() {
    tda_Arr x = {};
    tda_arr_deinit(&x);
    tda_arr_deinit(&x);
}

/* ========== raw ========== */

// the block changes hands both ways without a call to the allocator
static void test_into_raw_then_init_raw_moves_the_block() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al, &a, 1, 2, 3));
    const void *block = a.data_;
    const size_t requests = tda_test_probe_requests(&probe);

    const tda_SpanMut elems = tda_arr_into_raw(&a);
    TEST_ASSERT_EQUAL_PTR(block, elems.data);
    TEST_ASSERT_EQUAL_size_t(3, elems.len);
    TEST_ASSERT_NULL(a.al_.vt);
    TEST_ASSERT_NULL(a.data_);

    tda_Arr b = tda_arr_init_raw(elems, al);
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&b));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, &b, 2));
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    tda_arr_deinit(&b);
    tda_arr_deinit(&a); // inert after into_raw, so a no-op
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_into_raw_of_an_empty_arr_gives_no_block() {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_INIT_LEN(int32_t, 0, tda_al_default(), &a));

    const tda_SpanMut elems = tda_arr_into_raw(&a);
    TEST_ASSERT_NULL(elems.data);
    TEST_ASSERT_EQUAL_size_t(0, elems.len);

    tda_Arr b = tda_arr_init_raw(elems, tda_al_default());
    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&b));

    tda_arr_deinit(&b);
}

// a vec's block, shrunk to its length, is an arr's block: the two convert with no copy
static void test_a_vec_becomes_an_arr_with_no_copy() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, al, &v, 4, 5, 6));
    TDA_TEST_OK(tda_vec_shrink_to_fit(&v));
    const size_t requests = tda_test_probe_requests(&probe);

    tda_SpanMut elems;
    size_t cap;
    tda_vec_into_raw(&v, &elems, &cap);
    TEST_ASSERT_EQUAL_size_t(elems.len, cap);

    tda_Arr a = tda_arr_init_raw(elems, al);
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_INT32(4, *TDA_ARR_GET_AS(int32_t, &a, 0));
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    tda_arr_deinit(&a);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== from_data ========== */

static void test_from_data_copies_the_source() {
    constexpr int32_t src[4] = {5, 6, 7, 8};

    tda_Arr a = {};
    TDA_TEST_OK(tda_arr_from_data(src, 4, TDA_LAYOUT_OF(int32_t), tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_layout(&a).size);
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_arr_data(&a), 4);
    TEST_ASSERT_TRUE((const void *) src != tda_arr_data(&a));

    tda_arr_deinit(&a);
}

// the array owns a copy, it does not view the source
static void test_from_data_is_detached_from_the_source() {
    int32_t src[3] = {1, 2, 3};

    tda_Arr a = {};
    TDA_TEST_OK(tda_arr_from_data(src, 3, TDA_LAYOUT_OF(int32_t), tda_al_default(), &a));

    src[0] = 999;
    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, &a, 0));

    tda_arr_deinit(&a);
}

// a null source is legal while len == 0, as in tda_span_from_data
static void test_from_data_empty_has_no_buffer() {
    tda_Arr a = {};
    TDA_TEST_OK(tda_arr_from_data(nullptr, 0, TDA_LAYOUT_OF(int32_t), tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&a));
    TEST_ASSERT_NULL(tda_arr_data(&a));

    tda_arr_deinit(&a);
}

// elem_size drives the copy, so a type wider than a word must arrive whole
static void test_from_data_copies_whole_elements() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Arr arr = {};
    TDA_TEST_OK(tda_arr_from_data(src, 2, TDA_LAYOUT_OF(Pair), tda_al_default(), &arr));

    const Pair *got = tda_arr_data(&arr);
    TEST_ASSERT_EQUAL_INT64(1, got[0].a);
    TEST_ASSERT_EQUAL_INT64(2, got[0].b);
    TEST_ASSERT_EQUAL_INT64(3, got[1].a);
    TEST_ASSERT_EQUAL_INT64(4, got[1].b);

    tda_arr_deinit(&arr);
}

/* ========== access ========== */

static void test_set_get_roundtrip() {
    tda_Arr a = make_arr(5);

    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT32((int32_t) i, *TDA_ARR_GET_AS(int32_t, &a, i));
    }

    tda_arr_deinit(&a);
}

// a const pointer reads through the same typed macros: _Generic picks the const helper
static void test_typed_reads_through_a_const_pointer() {
    tda_Arr a;
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_al_default(), &a, 1, 2, 3));
    const tda_Arr *view = &a;

    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_FRONT_AS(int32_t, view));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, view, 1));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_BACK_AS(int32_t, view));

    tda_arr_deinit(&a);
}

static void test_get_mut_writes_through() {
    tda_Arr a = make_arr(3);

    *TDA_ARR_GET_MUT_AS(int32_t, &a, 1) = 42;
    TEST_ASSERT_EQUAL_INT32(42, *TDA_ARR_GET_AS(int32_t, &a, 1));

    tda_arr_deinit(&a);
}

// the value lies in the arr itself; with idx == the source slot the copy overlaps fully
static void test_set_from_its_own_elem() {
    tda_Arr a = make_arr(3);

    tda_arr_set(&a, 0, tda_arr_get(&a, 2));
    tda_arr_set(&a, 1, tda_arr_get(&a, 1));

    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, &a, 0));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, &a, 1));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, &a, 2));

    tda_arr_deinit(&a);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_Arr src = make_arr(4);

    tda_Arr dst = {};
    TDA_TEST_OK(tda_arr_copy(&src, &dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(&src), tda_arr_data(&dst), 4);
    TEST_ASSERT_TRUE(tda_arr_data(&src) != tda_arr_data(&dst));

    TDA_ARR_SET(int32_t, &src, 0, 999);
    TEST_ASSERT_EQUAL_INT32(0, *TDA_ARR_GET_AS(int32_t, &dst, 0));

    tda_arr_deinit(&dst);
    tda_arr_deinit(&src);
}

static void test_copy_assign_grow_shrink_empty() {
    tda_Arr src = make_arr(6), dst = make_arr(2);

    // grow: 2 -> 6
    TDA_TEST_OK(tda_arr_copy_assign(&dst, &src));
    TEST_ASSERT_EQUAL_size_t(6, tda_arr_len(&dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(&src), tda_arr_data(&dst), 6);

    // shrink: 6 -> 3
    tda_Arr small = make_arr(3);
    TDA_TEST_OK(tda_arr_copy_assign(&dst, &small));
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(&small), tda_arr_data(&dst), 3);

    // shrink to empty: the buffer is released
    tda_Arr empty = make_arr(0);
    TDA_TEST_OK(tda_arr_copy_assign(&dst, &empty));
    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&dst));
    TEST_ASSERT_NULL(tda_arr_data(&dst));

    tda_arr_deinit(&empty);
    tda_arr_deinit(&small);
    tda_arr_deinit(&dst);
    tda_arr_deinit(&src);
}

static void test_copy_assign_of_itself_changes_nothing() {
    tda_Arr a = make_arr(3);

    TDA_TEST_OK(tda_arr_copy_assign(&a, &a));
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, &a, 2));

    tda_arr_deinit(&a);
}

/* ========== mods / views ========== */

static void test_swap_exchanges_contents() {
    tda_Arr a = make_arr(2), b = make_arr(5);

    tda_arr_swap(&a, &b);

    TEST_ASSERT_EQUAL_size_t(5, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_size_t(2, tda_arr_len(&b));
    TEST_ASSERT_EQUAL_INT32(4, *TDA_ARR_GET_AS(int32_t, &a, 4));

    tda_arr_deinit(&b);
    tda_arr_deinit(&a);
}

static void test_span_views_the_same_memory() {
    tda_Arr a = make_arr(4);

    const tda_SpanMut s = tda_arr_to_span_mut(&a);
    TEST_ASSERT_EQUAL_PTR(tda_arr_data(&a), s.data);
    TEST_ASSERT_EQUAL_size_t(4, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem.size);

    TDA_SPAN_SET(int32_t, s, 0, 77);
    TEST_ASSERT_EQUAL_INT32(77, *TDA_ARR_GET_AS(int32_t, &a, 0));

    tda_arr_deinit(&a);
}

/* ========== from_span ========== */

static void test_from_span_copies_the_view() {
    constexpr int32_t src[3] = {7, 8, 9};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, src, 3);

    tda_Arr a = {};
    TDA_TEST_OK(tda_arr_from_span(s, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_layout(&a).size);
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_arr_data(&a), 3);
    TEST_ASSERT_TRUE((const void *) src != tda_arr_data(&a));

    tda_arr_deinit(&a);
}

static void test_from_span_empty_has_no_buffer() {
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, nullptr, 0);

    tda_Arr a = {};
    TDA_TEST_OK(tda_arr_from_span(s, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_layout(&a).size);
    TEST_ASSERT_NULL(tda_arr_data(&a));

    tda_arr_deinit(&a);
}

// arr -> span -> arr must round-trip without touching the original
static void test_from_span_of_an_arr_round_trips() {
    tda_Arr src = make_arr(4);

    tda_Arr dst = {};
    TDA_TEST_OK(tda_arr_from_span(tda_arr_to_span(&src), tda_al_default(), &dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(&src), tda_arr_data(&dst), 4);
    TEST_ASSERT_TRUE(tda_arr_data(&src) != tda_arr_data(&dst));

    tda_arr_deinit(&dst);
    tda_arr_deinit(&src);
}

/* ========== first / last ========== */

static void test_first_and_last_address_the_ends() {
    tda_Arr a = make_arr(4);

    TEST_ASSERT_EQUAL_INT32(0, *TDA_ARR_FRONT_AS(int32_t, &a));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_BACK_AS(int32_t, &a));
    TEST_ASSERT_EQUAL_PTR(tda_arr_data(&a), tda_arr_front(&a));
    TEST_ASSERT_EQUAL_PTR(TDA_ARR_GET_AS(int32_t, &a, 3), tda_arr_back(&a));

    tda_arr_deinit(&a);
}

static void test_first_and_last_mut_write_through() {
    tda_Arr a = make_arr(4);

    *TDA_ARR_FRONT_MUT_AS(int32_t, &a) = 10;
    *TDA_ARR_BACK_MUT_AS(int32_t, &a) = 20;

    TEST_ASSERT_EQUAL_INT32(10, *TDA_ARR_GET_AS(int32_t, &a, 0));
    TEST_ASSERT_EQUAL_INT32(20, *TDA_ARR_GET_AS(int32_t, &a, 3));

    tda_arr_deinit(&a);
}

static void test_first_and_last_coincide_on_a_single_elem() {
    tda_Arr a = make_arr(1);

    TEST_ASSERT_EQUAL_PTR(tda_arr_front(&a), tda_arr_back(&a));
    TEST_ASSERT_EQUAL_PTR(tda_arr_front_mut(&a), tda_arr_back_mut(&a));

    tda_arr_deinit(&a);
}

/* ========== data_mut / foreach ========== */

static void test_data_mut_writes_through() {
    tda_Arr a = make_arr(3);

    int32_t *d = tda_arr_data_mut(&a);
    d[2] = 99;

    TEST_ASSERT_EQUAL_INT32(99, *TDA_ARR_GET_AS(int32_t, &a, 2));

    tda_arr_deinit(&a);
}

/* ========== swap_elems ========== */

static void test_swap_elems_exchanges_the_pair() {
    tda_Arr a = make_arr(4);

    tda_arr_swap_elems(&a, 0, 3);

    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, &a, 0));
    TEST_ASSERT_EQUAL_INT32(0, *TDA_ARR_GET_AS(int32_t, &a, 3));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, &a, 1));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, &a, 2));

    tda_arr_deinit(&a);
}

static void test_swap_elems_same_index_is_noop() {
    tda_Arr a = make_arr(3);

    tda_arr_swap_elems(&a, 1, 1);

    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, &a, 1));

    tda_arr_deinit(&a);
}

// elem_size drives the swap, so a type wider than a word must move whole
static void test_swap_elems_moves_wide_elems_whole() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Arr arr = {};
    TDA_TEST_OK(TDA_ARR_FROM_DATA(Pair, src, 2, tda_al_default(), &arr));

    tda_arr_swap_elems(&arr, 0, 1);

    TEST_ASSERT_EQUAL_INT64(3, TDA_ARR_GET_AS(Pair, &arr, 0)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_ARR_GET_AS(Pair, &arr, 0)->b);
    TEST_ASSERT_EQUAL_INT64(1, TDA_ARR_GET_AS(Pair, &arr, 1)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_ARR_GET_AS(Pair, &arr, 1)->b);

    tda_arr_deinit(&arr);
}

/* ========== swap ========== */

static void test_swap_of_itself_changes_nothing() {
    tda_Arr a = make_arr(3);
    const void *before = tda_arr_data(&a);

    tda_arr_swap(&a, &a);

    TEST_ASSERT_EQUAL_PTR(before, tda_arr_data(&a));
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, &a, 2));

    tda_arr_deinit(&a);
}

// one allocator on both sides: the buffers are handed over, never copied
static void test_swap_same_allocator_hands_over_buffers() {
    tda_Arr a = make_arr(2), b = make_arr(5);

    const void *pa = tda_arr_data(&a), *pb = tda_arr_data(&b);

    tda_arr_swap(&a, &b);

    TEST_ASSERT_EQUAL_PTR(pb, tda_arr_data(&a));
    TEST_ASSERT_EQUAL_PTR(pa, tda_arr_data(&b));

    tda_arr_deinit(&b);
    tda_arr_deinit(&a);
}

/* ========== to span ========== */

static void test_to_span_matches_the_arr_shape() {
    tda_Arr a = make_arr(4);

    const tda_Span s = tda_arr_to_span(&a);

    TEST_ASSERT_EQUAL_PTR(tda_arr_data(&a), s.data);
    TEST_ASSERT_EQUAL_size_t(4, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem.size);
    TEST_ASSERT_EQUAL_INT32(2, *TDA_SPAN_GET_AS(int32_t, s, 2));

    tda_arr_deinit(&a);
}

static void test_to_span_of_empty_keeps_the_elem_layout() {
    tda_Arr a = make_arr(0);

    const tda_Span s = tda_arr_to_span(&a);

    TEST_ASSERT_NULL(s.data);
    TEST_ASSERT_EQUAL_size_t(0, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem.size);

    tda_arr_deinit(&a);
}

/* ========== allocators ========== */

static void test_copy_inherits_the_source_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Arr src = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_arena_to_al(&arena), &src, 1, 2, 3));

    tda_Arr dst = {};
    TDA_TEST_OK(tda_arr_copy(&src, &dst));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_arr_al(&dst)));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(&src), tda_arr_data(&dst), 3);

    tda_arr_deinit(&dst);
    tda_arr_deinit(&src);
    tda_arena_deinit(&arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Arr src = make_arr(4);

    tda_Arr dst = {};
    TDA_TEST_OK(tda_arr_copy_with(&src, tda_arena_to_al(&arena), &dst));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_arr_al(&dst)));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_arr_al(&src)));
    TEST_ASSERT_TRUE(tda_arr_eq(&src, &dst));

    // the copy outlives the source, so it owns its elems
    tda_arr_deinit(&src);
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, &dst, 3));

    tda_arr_deinit(&dst);
    tda_arena_deinit(&arena);
}

// the blocks come from the copy's allocator, not the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));
    tda_test_arena_leave(&arena, 0);

    tda_Arr src = make_arr(4);

    tda_Arr dst;
    TDA_TEST_POISON(dst);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_copy_with(&src, tda_arena_to_al(&arena), &dst));
    TDA_TEST_ASSERT_POISONED(dst);
    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&src));

    tda_arr_deinit(&src);
    tda_arena_deinit(&arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr src = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al, &src, 1, 2, 3));

    tda_Arr dst = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al, &dst, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    tda_arr_move_assign(&dst, &src);

    // no allocation: the block itself is handed over
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&dst));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, &dst, 1));

    // the source is left empty and usable
    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&src));
    TEST_ASSERT_NULL(tda_arr_data(&src));

    tda_arr_deinit(&src);
    tda_arr_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the allocator is part of the value: 'dst' takes the elems with the allocator they came
// from
static void test_move_assign_across_allocators_hands_the_allocator_over() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Arr src = make_arr(4);

    tda_Arr dst = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_arena_to_al(&arena), &dst, 9));

    tda_arr_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&dst));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, &dst, 3));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_arr_al(&dst)));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&src));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_arr_al(&src)));

    tda_arr_deinit(&src);
    tda_arr_deinit(&dst);
    tda_arena_deinit(&arena);
}

// nothing is allocated, so an exhausted allocator on either side cannot stop it
static void test_move_assign_needs_no_memory() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Arr dst = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_arena_to_al(&arena), &dst, 9));
    tda_test_arena_leave(&arena, 0);

    tda_Arr src = make_arr(4);

    tda_arr_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&dst));
    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&src));

    tda_arr_deinit(&src);
    tda_arr_deinit(&dst);
    tda_arena_deinit(&arena);
}

// an inert target, `= {}`, takes on everything the source has: layout, allocator and
// all, and the source stays usable
static void test_move_assign_fills_an_inert_target() {
    tda_Arr src = make_arr(4), dst = {};

    tda_arr_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&dst));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, &dst, 3));
    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&src));

    tda_arr_deinit(&src);
    tda_arr_deinit(&dst);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_Arr a = make_arr(3);

    tda_arr_move_assign(&a, &a);

    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_ARR_GET_AS(int32_t, &a, 2));

    tda_arr_deinit(&a);
}

// assignment resizes through the target's allocator, not the source's
static void test_copy_assign_keeps_the_target_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Arr src = make_arr(4);

    tda_Arr dst = {};
    TDA_TEST_OK(TDA_ARR_INIT_LEN(int32_t, 1, tda_arena_to_al(&arena), &dst));

    TDA_TEST_OK(tda_arr_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_size_t(4, tda_arr_len(&dst));
    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_arr_al(&dst)));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_arr_data(&src), tda_arr_data(&dst), 4);

    tda_arr_deinit(&dst);
    tda_arr_deinit(&src);
    tda_arena_deinit(&arena);
}

/* ========== allocation failure ========== */

// a 2-byte elem: SIZE_MAX of them is too many for any address space
static constexpr tda_Layout TWO_BYTES = {.size = 2, .align = 2};

// len * elem.size overflows size_t: reported, never attempted
static void test_init_len_reports_size_overflow() {
    tda_Arr a = {};

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_init_len(SIZE_MAX, TWO_BYTES, tda_al_default(), &a));

    TEST_ASSERT_NULL(a.al_.vt); // out is untouched on failure
}

static void test_from_data_reports_size_overflow() {
    constexpr int32_t src[1] = {1};
    tda_Arr a = {};

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_from_data(src, SIZE_MAX, TWO_BYTES, tda_al_default(), &a));

    TEST_ASSERT_NULL(a.al_.vt);
}

static void test_init_len_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(64, tda_al_default(), &arena));

    tda_Arr a;

    TDA_TEST_POISON(a);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_ARR_INIT_LEN(int32_t, 1000, tda_arena_to_al(&arena), &a));

    TDA_TEST_ASSERT_POISONED(a);

    tda_arena_deinit(&arena);
}

static void test_from_data_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(128, tda_al_default(), &arena));

    constexpr int32_t src[4] = {1, 2, 3, 4};
    tda_Arr a = {};

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_from_data(src, 1000, TDA_LAYOUT_OF(int32_t), tda_arena_to_al(&arena), &a));

    TEST_ASSERT_NULL(a.al_.vt);

    tda_arena_deinit(&arena);
}

// a copy takes both blocks from the SOURCE's allocator, so exhausting that arena fails it
static void test_copy_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    tda_Arr src = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_arena_to_al(&arena), &src, 1, 2, 3));
    tda_test_arena_leave(&arena, 0);

    tda_Arr dst;
    TDA_TEST_POISON(dst);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_copy(&src, &dst));

    TDA_TEST_ASSERT_POISONED(dst);
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&src)); // the source is only read

    tda_arena_deinit(&arena);
}

// an empty arr owns no block, so even an exhausted allocator copies it
static void test_copy_of_empty_allocates_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(128, tda_al_default(), &arena));

    tda_Arr src = {};
    TDA_TEST_OK(TDA_ARR_INIT_LEN(int32_t, 0, tda_arena_to_al(&arena), &src));
    tda_test_arena_leave(&arena, 0);

    tda_Arr dst = {};
    TDA_TEST_OK(tda_arr_copy(&src, &dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_len(&dst));
    tda_arr_deinit(&dst);
    tda_arr_deinit(&src);

    tda_arena_deinit(&arena);
}

// A copy_assign that changes the length resizes the TARGET's buffer, its only allocation.
// When that is refused the target keeps its old length, block and elems.
static void test_copy_assign_reports_a_refused_block_and_changes_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    tda_Arr other = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_arena_to_al(&arena), &other, 7, 8));

    tda_Arr self = make_arr(8); // default allocator, 0 .. 7

    const void *before = tda_arr_data(&other);
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arr_copy_assign(&other, &self));

    TEST_ASSERT_EQUAL_size_t(2, tda_arr_len(&other));
    TEST_ASSERT_EQUAL_PTR(before, tda_arr_data(&other));
    TEST_ASSERT_EQUAL_INT32(7, *TDA_ARR_GET_AS(int32_t, &other, 0));
    TEST_ASSERT_EQUAL_INT32(8, *TDA_ARR_GET_AS(int32_t, &other, 1));

    tda_arr_deinit(&self);
    tda_arena_deinit(&arena);
}

// equal lengths need no new room, so the elems overwrite the target's own block;
// the probe makes "no allocation" checkable
static void test_copy_assign_of_the_same_length_never_allocates() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr self = {}, other = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al, &self, 1, 2, 3));
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al, &other, 9, 9, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    const void *before = tda_arr_data(&other);

    TDA_TEST_OK(tda_arr_copy_assign(&other, &self));

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_PTR(before, tda_arr_data(&other));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_ARR_GET_AS(int32_t, &other, 0));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_ARR_GET_AS(int32_t, &other, 2));

    tda_arr_deinit(&self);
    tda_arr_deinit(&other);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_a_refused_buffer_leaves_nothing_behind() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 0);

    tda_Arr a;
    TDA_TEST_POISON(a);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_ARR_INIT_LEN(int32_t, 4, al, &a));

    TDA_TEST_ASSERT_POISONED(a);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_a_refused_buffer_of_from_data_leaves_nothing_behind() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 0);

    tda_Arr a = {};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_ARR_OF(int32_t, al, &a, 1, 2, 3));

    TEST_ASSERT_NULL(a.al_.vt);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// deinit must free the block of a filled arr; the default allocator cannot show that,
// so a probe counts it
static void test_deinit_hands_back_everything() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al, &a, 1, 2, 3, 4));
    TEST_ASSERT_EQUAL_size_t(1, probe.live);

    tda_arr_deinit(&a);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
}

static void test_an_empty_arr_owns_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_INIT_LEN(int32_t, 0, al, &a));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    tda_arr_deinit(&a);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);
}

/* ========== macros ========== */

static void test_macro_of_builds_from_literals() {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_al_default(), &a, 4, 5, 6));

    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_layout(&a).size);

    constexpr int32_t want[3] = {4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_arr_data(&a), 3);

    tda_arr_deinit(&a);
}

static void test_macro_of_derives_len_from_the_list() {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_OF(int64_t, tda_al_default(), &a, 1, 2, 3, 4, 5));

    TEST_ASSERT_EQUAL_size_t(5, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int64_t), tda_arr_elem_layout(&a).size);

    tda_arr_deinit(&a);
}

static void test_macro_from_data_takes_the_layout_from_the_type() {
    constexpr int32_t src[2] = {1, 2};

    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_FROM_DATA(int32_t, src, 2, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(2, tda_arr_len(&a));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_arr_elem_layout(&a).size);
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_arr_data(&a), 2);

    tda_arr_deinit(&a);
}

/* ========== bytes ========== */

static void test_bytes_is_len_times_elem_size() {
    tda_Arr a = make_arr(4);

    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), tda_arr_bytes(&a));

    tda_arr_deinit(&a);
}

static void test_bytes_of_empty_is_zero() {
    tda_Arr a = make_arr(0);

    TEST_ASSERT_EQUAL_size_t(0, tda_arr_bytes(&a));

    tda_arr_deinit(&a);
}

// elem_size, not the elem count, drives the total
static void test_bytes_tracks_elem_size() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_FROM_DATA(Pair, src, 2, tda_al_default(), &a));

    TEST_ASSERT_EQUAL_size_t(2 * sizeof(Pair), tda_arr_bytes(&a));

    tda_arr_deinit(&a);
}

// the arr and its view must agree on the size of the same memory
static void test_bytes_agrees_with_the_span() {
    tda_Arr a = make_arr(3);

    TEST_ASSERT_EQUAL_size_t(tda_span_bytes(tda_arr_to_span(&a)), tda_arr_bytes(&a));

    tda_arr_deinit(&a);
}

/* ========== compare ========== */

static void test_eq_matches_the_same_elems() {
    tda_Arr a = make_arr(4), b = make_arr(4);

    TEST_ASSERT_TRUE(tda_arr_eq(&a, &a));
    TEST_ASSERT_TRUE(tda_arr_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_arr_eq(&b, &a));
    TEST_ASSERT_TRUE(tda_arr_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_arr_deinit(&a);
    tda_arr_deinit(&b);
}

static void test_eq_parts_one_differing_elem() {
    tda_Arr a = make_arr(4), b = make_arr(4);
    TDA_ARR_SET(int32_t, &b, 3, 99);

    TEST_ASSERT_FALSE(tda_arr_eq(&a, &b));
    TEST_ASSERT_FALSE(tda_arr_eq(&b, &a));
    TEST_ASSERT_FALSE(tda_arr_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_arr_deinit(&a);
    tda_arr_deinit(&b);
}

// one is a prefix of the other, so only the length tells them apart
static void test_eq_parts_different_lengths() {
    tda_Arr a = make_arr(4);
    tda_Arr shorter = make_arr(3);

    TEST_ASSERT_FALSE(tda_arr_eq(&a, &shorter));
    TEST_ASSERT_FALSE(tda_arr_eq(&shorter, &a));
    TEST_ASSERT_FALSE(tda_arr_eq_by(&a, &shorter, tda_eq_i32, nullptr));

    tda_arr_deinit(&a);
    tda_arr_deinit(&shorter);
}

static void test_eq_of_two_empties() {
    tda_Arr a = make_arr(0);
    tda_Arr b = make_arr(0);
    tda_Arr one = make_arr(1);

    TEST_ASSERT_TRUE(tda_arr_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_arr_eq_by(&a, &b, tda_eq_i32, nullptr));
    TEST_ASSERT_FALSE(tda_arr_eq(&a, &one));
    TEST_ASSERT_FALSE(tda_arr_eq(&one, &a));

    tda_arr_deinit(&a);
    tda_arr_deinit(&b);
    tda_arr_deinit(&one);
}

// the caller's equality decides even where the bytes differ: these Pairs agree in the
// first field and differ in the second
static void test_eq_by_asks_the_equality() {
    constexpr Pair lhs[2] = {{1, 10}, {2, 20}};
    constexpr Pair rhs[2] = {{1, 70}, {2, 80}};

    tda_Arr a = {}, b = {};
    TDA_TEST_OK(TDA_ARR_FROM_DATA(Pair, lhs, 2, tda_al_default(), &a));
    TDA_TEST_OK(TDA_ARR_FROM_DATA(Pair, rhs, 2, tda_al_default(), &b));

    TEST_ASSERT_FALSE(tda_arr_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_arr_eq_by(&a, &b, tda_test_pair_eq_a, nullptr));

    tda_arr_deinit(&a);
    tda_arr_deinit(&b);
}

/* ========== print ========== */

// printers write to a stream, so cases read it back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_Arr *a) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_arr_fprint(a, stream, tda_fprint_i32, nullptr);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_elems() {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_al_default(), &a, 5, 3, 1));

    assert_prints("[5, 3, 1]", &a);

    tda_arr_deinit(&a);
}

static void test_fprint_of_a_single_elem_has_no_separator() {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_al_default(), &a, 7));

    assert_prints("[7]", &a);

    tda_arr_deinit(&a);
}

static void test_fprint_of_an_empty_arr() {
    tda_Arr a = make_arr(0);

    assert_prints("[]", &a);

    tda_arr_deinit(&a);
}

// stdout cannot be captured portably, so this only checks that the stdout variant runs
static void test_print_writes_to_stdout() {
    tda_Arr a = make_arr(3);

    tda_arr_print(&a, tda_fprint_i32, nullptr);

    tda_arr_deinit(&a);
}

// each side keeps the allocator its elems came from, and each is given back to it
static void test_swap_across_allocators_carries_each_allocator() {
    tda_TestProbe pa, pb;
    tda_test_probe_reset(&pa);
    tda_test_probe_reset(&pb);
    tda_Al al_a = tda_test_probe_full(&pa), al_b = tda_test_probe_full(&pb);

    tda_Arr a = {}, b = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al_a, &a, 1, 2));
    TDA_TEST_OK(TDA_ARR_OF(int32_t, al_b, &b, 3, 4, 5));

    tda_arr_swap(&a, &b);

    TEST_ASSERT_TRUE(tda_al_eq(al_b, tda_arr_al(&a)));
    TEST_ASSERT_TRUE(tda_al_eq(al_a, tda_arr_al(&b)));
    TEST_ASSERT_EQUAL_size_t(3, tda_arr_len(&a));

    tda_arr_deinit(&a);
    TEST_ASSERT_EQUAL_size_t(0, pb.live);
    tda_arr_deinit(&b);
    TEST_ASSERT_EQUAL_size_t(0, pa.live);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_Arr x = {};
    TDA_TEST_OK(TDA_ARR_OF(int32_t, tda_al_default(), &x, 1, 2));

    tda_arr_deinit(&x);
    TEST_ASSERT_NULL(x.al_.vt);

    tda_arr_deinit(&x);
}


/* ========== print errors ========== */

// the ctx reaches every call, every elem is still asked after a failure, and the first
// failure is what comes back; print adds only its newline
static void test_fprint_hands_on_the_ctx_and_the_first_failure() {
    tda_Arr a = make_arr(3);
    tda_TestPrint pr = {.fail_from = 2};
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    TEST_ASSERT_EQUAL_INT(-2, tda_arr_fprint(&a, stream, tda_test_fprint_i32_failing, &pr));
    TEST_ASSERT_EQUAL_size_t(3, pr.calls);
    fclose(stream);

    pr = (tda_TestPrint){.fail_from = 0};
    TEST_ASSERT_EQUAL_INT(0, tda_arr_print(&a, tda_test_fprint_i32_failing, &pr));
    pr = (tda_TestPrint){.fail_from = 2};
    TEST_ASSERT_EQUAL_INT(-2, tda_arr_print(&a, tda_test_fprint_i32_failing, &pr));

    FILE *ro = tda_test_unwritable_stream();
    TEST_ASSERT_LESS_THAN_INT(0, tda_arr_fprint(&a, ro, tda_fprint_i32, nullptr));
    fclose(ro);

    tda_arr_deinit(&a);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_sets_shape_and_zeroes);
    RUN_TEST(test_init_empty_has_no_buffer);
    RUN_TEST(test_deinit_of_an_inert_value_is_noop);
    RUN_TEST(test_deinit_twice_is_noop);

    RUN_TEST(test_into_raw_then_init_raw_moves_the_block);
    RUN_TEST(test_into_raw_of_an_empty_arr_gives_no_block);
    RUN_TEST(test_a_vec_becomes_an_arr_with_no_copy);

    RUN_TEST(test_from_data_copies_the_source);
    RUN_TEST(test_from_data_is_detached_from_the_source);
    RUN_TEST(test_from_data_empty_has_no_buffer);
    RUN_TEST(test_from_data_copies_whole_elements);

    RUN_TEST(test_set_get_roundtrip);
    RUN_TEST(test_typed_reads_through_a_const_pointer);
    RUN_TEST(test_get_mut_writes_through);
    RUN_TEST(test_set_from_its_own_elem);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_assign_grow_shrink_empty);
    RUN_TEST(test_copy_assign_of_itself_changes_nothing);

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

    RUN_TEST(test_swap_of_itself_changes_nothing);
    RUN_TEST(test_swap_same_allocator_hands_over_buffers);

    RUN_TEST(test_to_span_matches_the_arr_shape);
    RUN_TEST(test_to_span_of_empty_keeps_the_elem_layout);

    RUN_TEST(test_copy_inherits_the_source_allocator);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_hands_the_allocator_over);
    RUN_TEST(test_move_assign_needs_no_memory);
    RUN_TEST(test_move_assign_fills_an_inert_target);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_assign_keeps_the_target_allocator);

    RUN_TEST(test_init_len_reports_size_overflow);
    RUN_TEST(test_from_data_reports_size_overflow);
    RUN_TEST(test_init_len_reports_an_exhausted_arena);
    RUN_TEST(test_from_data_reports_an_exhausted_arena);
    RUN_TEST(test_copy_reports_an_exhausted_arena);
    RUN_TEST(test_copy_of_empty_allocates_nothing);
    RUN_TEST(test_copy_assign_reports_a_refused_block_and_changes_nothing);
    RUN_TEST(test_copy_assign_of_the_same_length_never_allocates);
    RUN_TEST(test_a_refused_buffer_leaves_nothing_behind);
    RUN_TEST(test_a_refused_buffer_of_from_data_leaves_nothing_behind);
    RUN_TEST(test_deinit_hands_back_everything);
    RUN_TEST(test_an_empty_arr_owns_nothing);

    RUN_TEST(test_macro_of_builds_from_literals);
    RUN_TEST(test_macro_of_derives_len_from_the_list);
    RUN_TEST(test_macro_from_data_takes_the_layout_from_the_type);

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

    RUN_TEST(test_swap_across_allocators_carries_each_allocator);

    RUN_TEST(test_fprint_hands_on_the_ctx_and_the_first_failure);

    return UNITY_END();
}
