#include "tda/ds/vec.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/print.h"

#include "support/arena.h"
#include "support/pair.h"
#include "support/probe.h"
#include "support/status.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// the arena hands out nothing finer than this, so a request is charged rounded up
static constexpr size_t ARENA_STEP = alignof(max_align_t);

static size_t arena_charge(size_t bytes) {
    return (bytes + ARENA_STEP - 1) / ARENA_STEP * ARENA_STEP;
}

// int32_t vec of len elems holding 0, 1, ... len-1; cap == len
static tda_Vec *make_vec(size_t len) {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_LEN(int32_t, len, tda_al_default(), &v));

    for (size_t i = 0; i < len; ++i) {
        TDA_VEC_SET(int32_t, v, i, (int32_t) i);
    }
    return v;
}

// empty int32_t vec with room for cap elems
static tda_Vec *make_vec_cap(size_t cap) {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_CAP(int32_t, cap, tda_al_default(), &v));

    return v;
}

static void push_int(tda_Vec *v, int32_t val) {
    TDA_TEST_OK(tda_vec_push(v, &val));
}

static void assert_elems(const tda_Vec *v, const int32_t *want, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, tda_vec_len(v));

    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_EQUAL_INT32(want[i], *TDA_VEC_GET_AS(int32_t, v, i));
    }
}

/* ========== lifetime ========== */

static void test_new_starts_empty_and_unallocated() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_size(v));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_vec_al(v));
    TEST_ASSERT_NULL(tda_vec_data(v));

    tda_vec_drop(v);
}

static void test_new_len_sets_len_cap_and_zeroes() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_LEN(int32_t, 4, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(v));

    constexpr int32_t zeroes[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(zeroes, tda_vec_data(v), 4);

    tda_vec_drop(v);
}

// cap is room, not content: the vec is still empty
static void test_new_cap_reserves_without_length() {
    tda_Vec *v = make_vec_cap(8);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(v));
    TEST_ASSERT_NOT_NULL(tda_vec_data(v));

    tda_vec_drop(v);
}

static void test_new_cap_zero_has_no_buffer() {
    tda_Vec *v = make_vec_cap(0);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(v));
    TEST_ASSERT_NULL(tda_vec_data(v));

    tda_vec_drop(v);
}

static void test_from_data_copies_and_detaches_the_source() {
    int32_t src[3] = {5, 6, 7};

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_FROM_DATA(int32_t, src, 3, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_vec_data(v), 3);
    TEST_ASSERT_TRUE((const void *) src != tda_vec_data(v));

    src[0] = 999;
    TEST_ASSERT_EQUAL_INT32(5, *TDA_VEC_GET_AS(int32_t, v, 0));

    tda_vec_drop(v);
}

// null source is legal while len == 0 — same rule as tda_span_from_data
static void test_from_data_empty_has_no_buffer() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(tda_vec_from_data(nullptr, 0, sizeof(int32_t), tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(v));
    TEST_ASSERT_NULL(tda_vec_data(v));

    tda_vec_drop(v);
}

// elem_size drives the copy, so a type wider than a word must arrive whole
static void test_from_data_copies_whole_elems() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_vec_elem_size(v));
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, v, 1)->b);

    tda_vec_drop(v);
}

static void test_from_span_copies_the_view() {
    constexpr int32_t src[3] = {7, 8, 9};

    tda_Vec *v = nullptr;
    TDA_TEST_OK(tda_vec_from_span(TDA_SPAN_FROM_DATA(int32_t, src, 3), tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_size(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_vec_data(v), 3);
    TEST_ASSERT_TRUE((const void *) src != tda_vec_data(v));

    tda_vec_drop(v);
}

static void test_drop_null_is_noop() {
    tda_vec_drop(nullptr);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_Vec *src = make_vec(4);

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(tda_vec_copy(src, &dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(src), tda_vec_data(dst), 4);
    TEST_ASSERT_TRUE(tda_vec_data(src) != tda_vec_data(dst));

    TDA_VEC_SET(int32_t, src, 0, 999);
    TEST_ASSERT_EQUAL_INT32(0, *TDA_VEC_GET_AS(int32_t, dst, 0));

    tda_vec_drop(dst);
    tda_vec_drop(src);
}

// the copy is sized to the content, so the source's spare capacity is not carried over
static void test_copy_fits_the_len_not_the_source_capacity() {
    tda_Vec *src = make_vec_cap(8);
    push_int(src, 1);
    push_int(src, 2);

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(tda_vec_copy(src, &dst));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_cap(dst));

    tda_vec_drop(dst);
    tda_vec_drop(src);
}

static void test_copy_of_empty_has_no_buffer() {
    tda_Vec *src = make_vec(0);

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(tda_vec_copy(src, &dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_size(dst));
    TEST_ASSERT_NULL(tda_vec_data(dst));

    tda_vec_drop(dst);
    tda_vec_drop(src);
}

static void test_copy_inherits_the_source_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *src = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, arena, &src, 1, 2, 3));

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(tda_vec_copy(src, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_vec_al(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(src), tda_vec_data(dst), 3);

    tda_vec_drop(dst);
    tda_vec_drop(src);
    tda_al_arena_drop(arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *src = make_vec(4);

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(tda_vec_copy_with(src, arena, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_vec_al(dst));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_vec_al(src));
    TEST_ASSERT_TRUE(tda_vec_eq(src, dst));

    // the source is gone and the copy still holds the elems: they were taken, not viewed
    tda_vec_drop(src);
    TEST_ASSERT_EQUAL_INT32(3, *TDA_VEC_GET_AS(int32_t, dst, 3));

    tda_vec_drop(dst);
    tda_al_arena_drop(arena);
}

// the blocks are asked of the allocator the copy is going to, not of the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    tda_Vec *src = make_vec(4);

    tda_Vec *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_copy_with(src, arena, &dst));
    TEST_ASSERT_NULL(dst);
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(src));

    tda_vec_drop(src);
    tda_al_arena_drop(arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec *src = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, &al, &src, 1, 2, 3));

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, &al, &dst, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_vec_move_assign(src, dst));

    // nothing was asked of the allocator: the block changed hands, capacity and all
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, dst, 1));

    // the source is left empty and usable, its block handed back
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(src));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(src));
    TEST_ASSERT_NULL(tda_vec_data(src));

    tda_vec_drop(src);
    tda_vec_drop(dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_move_assign_across_allocators_empties_the_source() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *src = make_vec(4);

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, arena, &dst, 9));

    TDA_TEST_OK(tda_vec_move_assign(src, dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_VEC_GET_AS(int32_t, dst, 3));
    TEST_ASSERT_EQUAL_PTR(arena, tda_vec_al(dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(src));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_vec_al(src));

    tda_vec_drop(src);
    tda_vec_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_across_allocators_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, arena, &dst, 9));
    tda_test_arena_leave(arena, 0);

    tda_Vec *src = make_vec(4);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_move_assign(src, dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(src));
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_INT32(9, *TDA_VEC_GET_AS(int32_t, dst, 0));

    tda_vec_drop(src);
    tda_vec_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_Vec *v = make_vec(3);

    TDA_TEST_OK(tda_vec_move_assign(v, v));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, v, 2));

    tda_vec_drop(v);
}

static void test_copy_assign_grows_and_shrinks_the_len() {
    tda_Vec *src = make_vec(6);
    tda_Vec *dst = make_vec(2);

    // grow: 2 -> 6
    TDA_TEST_OK(tda_vec_copy_assign(src, dst));
    TEST_ASSERT_EQUAL_size_t(6, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(src), tda_vec_data(dst), 6);

    // shrink: 6 -> 3
    tda_Vec *small = make_vec(3);
    TDA_TEST_OK(tda_vec_copy_assign(small, dst));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(small), tda_vec_data(dst), 3);

    // shrink to empty: the len goes, the buffer stays
    tda_Vec *empty = make_vec(0);
    TDA_TEST_OK(tda_vec_copy_assign(empty, dst));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(dst));
    TEST_ASSERT_NOT_NULL(tda_vec_data(dst));

    tda_vec_drop(empty);
    tda_vec_drop(small);
    tda_vec_drop(dst);
    tda_vec_drop(src);
}

// unlike arr, assignment never gives capacity back — that is what shrink_to_fit is for
static void test_copy_assign_keeps_the_target_capacity() {
    tda_Vec *src = make_vec(2);
    tda_Vec *dst = make_vec(6);

    TDA_TEST_OK(tda_vec_copy_assign(src, dst));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_size_t(6, tda_vec_cap(dst));

    tda_vec_drop(dst);
    tda_vec_drop(src);
}

static void test_copy_assign_self_is_noop() {
    tda_Vec *v = make_vec(3);

    TDA_TEST_OK(tda_vec_copy_assign(v, v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, v, 2));

    tda_vec_drop(v);
}

// assignment resizes through the target's allocator, not the source's
static void test_copy_assign_keeps_the_target_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *src = make_vec(4);

    tda_Vec *dst = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_LEN(int32_t, 1, arena, &dst));

    TDA_TEST_OK(tda_vec_copy_assign(src, dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(dst));
    TEST_ASSERT_EQUAL_PTR(arena, tda_vec_al(dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(src), tda_vec_data(dst), 4);

    tda_vec_drop(dst);
    tda_vec_drop(src);
    tda_al_arena_drop(arena);
}

/* ========== info ========== */

static void test_bytes_is_len_times_elem_size() {
    tda_Vec *v = make_vec(4);

    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), tda_vec_bytes(v));

    tda_vec_drop(v);
}

// bytes measures the content, not the allocation — spare capacity does not count
static void test_bytes_ignores_the_spare_capacity() {
    tda_Vec *v = make_vec_cap(8);
    push_int(v, 1);
    push_int(v, 2);
    push_int(v, 3);

    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_size_t(3 * sizeof(int32_t), tda_vec_bytes(v));

    tda_vec_drop(v);
}

static void test_bytes_of_empty_is_zero() {
    tda_Vec *v = make_vec_cap(8);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_bytes(v));

    tda_vec_drop(v);
}

// elem_size, not the elem count, drives the total
static void test_bytes_tracks_elem_size() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(2 * sizeof(Pair), tda_vec_bytes(v));

    tda_vec_drop(v);
}

// the vec and its view must agree on the size of the same memory
static void test_bytes_agrees_with_the_span() {
    tda_Vec *v = make_vec_cap(8);
    push_int(v, 1);
    push_int(v, 2);

    TEST_ASSERT_EQUAL_size_t(tda_span_bytes(tda_vec_to_span(v)), tda_vec_bytes(v));

    tda_vec_drop(v);
}

/* ========== access ========== */

static void test_set_get_roundtrip() {
    tda_Vec *v = make_vec(5);

    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT32((int32_t) i, *TDA_VEC_GET_AS(int32_t, v, i));
    }

    tda_vec_drop(v);
}

static void test_get_mut_writes_through() {
    tda_Vec *v = make_vec(3);

    *TDA_VEC_GET_MUT_AS(int32_t, v, 1) = 42;
    TEST_ASSERT_EQUAL_INT32(42, *TDA_VEC_GET_AS(int32_t, v, 1));

    tda_vec_drop(v);
}

static void test_first_and_last_address_the_ends() {
    tda_Vec *v = make_vec(4);

    TEST_ASSERT_EQUAL_INT32(0, *TDA_VEC_FRONT_AS(int32_t, v));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_VEC_BACK_AS(int32_t, v));
    TEST_ASSERT_EQUAL_PTR(tda_vec_data(v), tda_vec_front(v));
    TEST_ASSERT_EQUAL_PTR(TDA_VEC_GET_AS(int32_t, v, 3), tda_vec_back(v));

    tda_vec_drop(v);
}

static void test_first_and_last_mut_write_through() {
    tda_Vec *v = make_vec(4);

    *TDA_VEC_FRONT_MUT_AS(int32_t, v) = 10;
    *TDA_VEC_BACK_MUT_AS(int32_t, v) = 20;

    TEST_ASSERT_EQUAL_INT32(10, *TDA_VEC_GET_AS(int32_t, v, 0));
    TEST_ASSERT_EQUAL_INT32(20, *TDA_VEC_GET_AS(int32_t, v, 3));

    tda_vec_drop(v);
}

static void test_first_and_last_coincide_on_a_single_elem() {
    tda_Vec *v = make_vec(1);

    TEST_ASSERT_EQUAL_PTR(tda_vec_front(v), tda_vec_back(v));
    TEST_ASSERT_EQUAL_PTR(tda_vec_front_mut(v), tda_vec_back_mut(v));

    tda_vec_drop(v);
}

// last follows the len, not the capacity
static void test_last_follows_the_len() {
    tda_Vec *v = make_vec_cap(8);
    push_int(v, 1);
    push_int(v, 2);

    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_BACK_AS(int32_t, v));
    TEST_ASSERT_EQUAL_PTR(TDA_VEC_GET_AS(int32_t, v, 1), tda_vec_back(v));

    tda_vec_drop(v);
}

static void test_data_mut_writes_through() {
    tda_Vec *v = make_vec(3);

    int32_t *d = tda_vec_data_mut(v);
    d[2] = 99;

    TEST_ASSERT_EQUAL_INT32(99, *TDA_VEC_GET_AS(int32_t, v, 2));

    tda_vec_drop(v);
}

/* ========== push / pop ========== */

// growth is 0 -> 1 and then doubling, and it only fires when the vec is full
static void test_push_appends_and_doubles_the_capacity() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    constexpr size_t want_cap[5] = {1, 2, 4, 4, 8};
    for (size_t i = 0; i < 5; ++i) {
        push_int(v, (int32_t) i);

        TEST_ASSERT_EQUAL_size_t(i + 1, tda_vec_len(v));
        TEST_ASSERT_EQUAL_size_t(want_cap[i], tda_vec_cap(v));
    }

    constexpr int32_t want[5] = {0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 5);

    tda_vec_drop(v);
}

// reserved room is used as is: no growth, no move
static void test_push_uses_reserved_capacity_without_reallocating() {
    tda_Vec *v = make_vec_cap(4);
    const void *before = tda_vec_data(v);

    for (int32_t i = 0; i < 4; ++i) {
        push_int(v, i);
    }

    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(v));

    tda_vec_drop(v);
}

static void test_push_moves_whole_elems() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(Pair, tda_al_default(), &v));

    TDA_TEST_OK(tda_vec_push(v, &(Pair){1, 2}));
    TDA_TEST_OK(tda_vec_push(v, &(Pair){3, 4}));

    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, v, 1)->b);

    tda_vec_drop(v);
}

static void test_pop_shortens_and_keeps_the_capacity() {
    tda_Vec *v = make_vec(3);
    const size_t cap = tda_vec_cap(v);

    tda_vec_pop(v);

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_VEC_BACK_AS(int32_t, v));

    tda_vec_pop(v);
    tda_vec_pop(v);
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));

    tda_vec_drop(v);
}

// the slot a pop released is the one the next push takes
static void test_push_after_pop_overwrites_the_slot() {
    tda_Vec *v = make_vec(3);
    const void *before = tda_vec_data(v);

    tda_vec_pop(v);
    push_int(v, 77);

    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32(77, *TDA_VEC_GET_AS(int32_t, v, 2));

    tda_vec_drop(v);
}

/* ========== insert / remove ========== */

static void test_insert_at_front_middle_and_end() {
    tda_Vec *v = make_vec(3); // 0, 1, 2

    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, v, 0, 10));
    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, v, 2, 20));
    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, v, 5, 30));

    constexpr int32_t want[6] = {10, 0, 20, 1, 2, 30};
    TEST_ASSERT_EQUAL_size_t(6, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 6);

    tda_vec_drop(v);
}

// idx == len on an empty vec is the only legal index there
static void test_insert_into_an_empty_vec() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, v, 0, 42));

    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_INT32(42, *TDA_VEC_GET_AS(int32_t, v, 0));

    tda_vec_drop(v);
}

// a full vec must grow before the tail is shifted, or the shift writes past the buffer
static void test_insert_grows_when_full() {
    tda_Vec *v = make_vec(2); // len == cap == 2

    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, v, 0, 9));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_TRUE(tda_vec_cap(v) >= 3);

    constexpr int32_t want[3] = {9, 0, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 3);

    tda_vec_drop(v);
}

static void test_remove_from_front_middle_and_end() {
    tda_Vec *v = make_vec(5); // 0, 1, 2, 3, 4

    tda_vec_remove(v, 0); // 1, 2, 3, 4
    tda_vec_remove(v, 1); // 1, 3, 4
    tda_vec_remove(v, 2); // 1, 3

    constexpr int32_t want[2] = {1, 3};
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 2);

    tda_vec_drop(v);
}

static void test_remove_of_the_last_elem_empties_the_vec() {
    tda_Vec *v = make_vec(1);

    tda_vec_remove(v, 0);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_cap(v));

    tda_vec_drop(v);
}

// the tail shift is sized in bytes, so wide elems must travel whole
static void test_insert_and_remove_move_whole_elems() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    TDA_TEST_OK(tda_vec_insert(v, 1, &(Pair){5, 6}));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT64(5, TDA_VEC_GET_AS(Pair, v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(6, TDA_VEC_GET_AS(Pair, v, 1)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, v, 2)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, v, 2)->b);

    tda_vec_remove(v, 0);

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT64(5, TDA_VEC_GET_AS(Pair, v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(6, TDA_VEC_GET_AS(Pair, v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, v, 1)->b);

    tda_vec_drop(v);
}

/* ========== bulk mods ========== */

static void test_extend_appends_in_order() {
    tda_Vec *v = make_vec(3); // 0, 1, 2
    constexpr int32_t src[3] = {7, 8, 9};

    TDA_TEST_OK(tda_vec_extend(v, TDA_SPAN_FROM_DATA(int32_t, src, 3)));

    constexpr int32_t want[6] = {0, 1, 2, 7, 8, 9};
    assert_elems(v, want, 6);

    tda_vec_drop(v);
}

static void test_extend_onto_an_empty_vec() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    constexpr int32_t src[2] = {4, 5};
    TDA_TEST_OK(tda_vec_extend(v, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    assert_elems(v, src, 2);

    tda_vec_drop(v);
}

// an empty run is not an error and not a no-op by accident: nothing is allocated and
// nothing moves
static void test_extend_with_an_empty_span_changes_nothing() {
    tda_Vec *v = make_vec(3);
    const size_t cap = tda_vec_cap(v);
    const void *before = tda_vec_data(v);

    TDA_TEST_OK(tda_vec_extend(v, TDA_SPAN_FROM_DATA(int32_t, nullptr, 0)));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    tda_vec_drop(v);
}

// the whole point of the operation: the room for the run is asked for ONCE. Eight pushes
// onto the same empty vec would walk the growth factor up and ask four times
static void test_extend_takes_the_room_once() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, &al, &v));

    const size_t before = tda_test_probe_requests(&probe);

    constexpr int32_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TDA_TEST_OK(tda_vec_extend(v, TDA_SPAN_FROM_DATA(int32_t, src, 8)));

    TEST_ASSERT_EQUAL_size_t(1, tda_test_probe_requests(&probe) - before);
    assert_elems(v, src, 8);

    tda_vec_drop(v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a run of one-elem extends must cost what a run of pushes costs — the growth factor,
// not one allocation per call. Asking for exactly the new length every time would make
// this 64 requests instead of a handful
static void test_a_run_of_extends_stays_amortized() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, &al, &v));

    for (int32_t i = 0; i < 64; ++i) {
        TDA_TEST_OK(tda_vec_extend(v, TDA_SPAN_FROM_DATA(int32_t, &i, 1)));
    }

    TEST_ASSERT_EQUAL_size_t(64, tda_vec_len(v));
    TEST_ASSERT_TRUE(tda_test_probe_requests(&probe) < 20);
    TEST_ASSERT_EQUAL_INT32(63, *TDA_VEC_GET_AS(int32_t, v, 63));

    tda_vec_drop(v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// resize(len + 1) is how a caller grows by one elem it then fills in place, so a run of
// them must cost what a run of pushes does, not one allocation per call
static void test_a_run_of_resizes_stays_amortized() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, &al, &v));

    for (size_t len = 1; len <= 64; ++len) {
        TDA_TEST_OK(tda_vec_resize(v, len));
    }

    TEST_ASSERT_EQUAL_size_t(64, tda_vec_len(v));
    TEST_ASSERT_TRUE(tda_test_probe_requests(&probe) < 20);

    tda_vec_drop(v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// alone on an arena the block is always the last one, so it grows in place: what the
// growth costs the arena is the final capacity, not every capacity passed on the way
static void test_growing_alone_on_an_arena_costs_only_the_capacity() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1 << 16);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, arena, &v));
    const size_t before = tda_al_arena_stats(arena).used;

    for (int32_t i = 0; i < 1000; ++i) {
        TDA_TEST_OK(tda_vec_push(v, &i));
    }

    TEST_ASSERT_EQUAL_size_t(1024, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_size_t(1024 * sizeof(int32_t), tda_al_arena_stats(arena).used - before);

    tda_vec_drop(v);
    tda_al_arena_drop(arena);
}

static void test_extend_moves_wide_elems_whole() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(Pair, tda_al_default(), &v));

    constexpr Pair src[2] = {{1, 10}, {2, 20}};
    TDA_TEST_OK(tda_vec_extend(v, TDA_SPAN_FROM_DATA(Pair, src, 2)));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT64(10, TDA_VEC_GET_AS(Pair, v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, v, 1)->a);

    tda_vec_drop(v);
}

static void test_insert_span_puts_the_run_before_the_index() {
    tda_Vec *v = make_vec(4); // 0, 1, 2, 3
    constexpr int32_t src[2] = {8, 9};

    TDA_TEST_OK(tda_vec_insert_span(v, 1, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    constexpr int32_t want[6] = {0, 8, 9, 1, 2, 3};
    assert_elems(v, want, 6);

    tda_vec_drop(v);
}

static void test_insert_span_at_the_front() {
    tda_Vec *v = make_vec(3);
    constexpr int32_t src[2] = {8, 9};

    TDA_TEST_OK(tda_vec_insert_span(v, 0, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    constexpr int32_t want[5] = {8, 9, 0, 1, 2};
    assert_elems(v, want, 5);

    tda_vec_drop(v);
}

// idx == len is the same operation extend performs, and extend is written as this call
static void test_insert_span_at_len_is_extend() {
    tda_Vec *v = make_vec(3);
    constexpr int32_t src[2] = {8, 9};

    TDA_TEST_OK(tda_vec_insert_span(v, tda_vec_len(v), TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    constexpr int32_t want[5] = {0, 1, 2, 8, 9};
    assert_elems(v, want, 5);

    tda_vec_drop(v);
}

static void test_insert_span_of_an_empty_span_changes_nothing() {
    tda_Vec *v = make_vec(3);
    const void *before = tda_vec_data(v);

    TDA_TEST_OK(tda_vec_insert_span(v, 1, TDA_SPAN_FROM_DATA(int32_t, nullptr, 0)));

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(v, want, 3);
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    tda_vec_drop(v);
}

// the tail is what a loop of insert would walk again for every elem; here it moves once,
// and a tail longer than the run is what tells a right shift from a wrong one
static void test_insert_span_keeps_a_long_tail_in_order() {
    tda_Vec *v = make_vec(16);
    constexpr int32_t src[2] = {100, 200};

    TDA_TEST_OK(tda_vec_insert_span(v, 2, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    TEST_ASSERT_EQUAL_size_t(18, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_VEC_GET_AS(int32_t, v, 1));
    TEST_ASSERT_EQUAL_INT32(100, *TDA_VEC_GET_AS(int32_t, v, 2));
    TEST_ASSERT_EQUAL_INT32(200, *TDA_VEC_GET_AS(int32_t, v, 3));
    for (size_t i = 4; i < 18; ++i) {
        TEST_ASSERT_EQUAL_INT32((int32_t) i - 2, *TDA_VEC_GET_AS(int32_t, v, i));
    }

    tda_vec_drop(v);
}

static void test_remove_range_closes_the_gap() {
    tda_Vec *v = make_vec(6); // 0 .. 5

    tda_vec_remove_range(v, 1, 3);

    constexpr int32_t want[3] = {0, 4, 5};
    assert_elems(v, want, 3);

    tda_vec_drop(v);
}

static void test_remove_range_at_the_front() {
    tda_Vec *v = make_vec(5);

    tda_vec_remove_range(v, 0, 2);

    constexpr int32_t want[3] = {2, 3, 4};
    assert_elems(v, want, 3);

    tda_vec_drop(v);
}

// nothing follows the gap, so there is no tail to move
static void test_remove_range_to_the_end() {
    tda_Vec *v = make_vec(5);

    tda_vec_remove_range(v, 3, 2);

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(v, want, 3);

    tda_vec_drop(v);
}

static void test_remove_range_of_zero_changes_nothing() {
    tda_Vec *v = make_vec(4);

    tda_vec_remove_range(v, 2, 0);

    constexpr int32_t want[4] = {0, 1, 2, 3};
    assert_elems(v, want, 4);

    tda_vec_drop(v);
}

static void test_remove_range_of_everything_empties_the_vec() {
    tda_Vec *v = make_vec(4);

    tda_vec_remove_range(v, 0, 4);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));

    tda_vec_drop(v);
}

// dropping elems hands nothing back to the allocator, as with pop and clear
static void test_remove_range_leaves_the_capacity_alone() {
    tda_Vec *v = make_vec(6);
    const size_t cap = tda_vec_cap(v);
    const void *before = tda_vec_data(v);

    tda_vec_remove_range(v, 1, 2);

    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    tda_vec_drop(v);
}

static void test_remove_range_moves_wide_elems_whole() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(Pair, tda_al_default(), &v));

    constexpr Pair src[4] = {{1, 10}, {2, 20}, {3, 30}, {4, 40}};
    TDA_TEST_OK(tda_vec_extend(v, TDA_SPAN_FROM_DATA(Pair, src, 4)));

    tda_vec_remove_range(v, 1, 2);

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(40, TDA_VEC_GET_AS(Pair, v, 1)->b);

    tda_vec_drop(v);
}

/* ========== clear ========== */

static void test_clear_drops_the_len_and_keeps_the_buffer() {
    tda_Vec *v = make_vec(4);
    const void *before = tda_vec_data(v);

    tda_vec_clear(v);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    // the room is still there, so pushing back needs no allocation
    push_int(v, 7);
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));
    TEST_ASSERT_EQUAL_INT32(7, *TDA_VEC_GET_AS(int32_t, v, 0));

    tda_vec_drop(v);
}

static void test_clear_of_an_empty_vec_is_a_noop() {
    tda_Vec *v = make_vec(0);

    tda_vec_clear(v);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));
    TEST_ASSERT_NULL(tda_vec_data(v));

    tda_vec_drop(v);
}

/* ========== reserve ========== */

static void test_reserve_grows_the_capacity_and_keeps_the_contents() {
    tda_Vec *v = make_vec(3);

    TDA_TEST_OK(tda_vec_reserve(v, 16));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(16, tda_vec_cap(v));

    constexpr int32_t want[3] = {0, 1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 3);

    tda_vec_drop(v);
}

// reserve never shrinks: a request at or below the current capacity changes nothing
static void test_reserve_below_the_capacity_is_a_noop() {
    tda_Vec *v = make_vec_cap(8);
    const void *before = tda_vec_data(v);

    TDA_TEST_OK(tda_vec_reserve(v, 2));
    TDA_TEST_OK(tda_vec_reserve(v, 8));
    TDA_TEST_OK(tda_vec_reserve(v, 0));

    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    tda_vec_drop(v);
}

// new_cap * elem_size overflows size_t: reported, never attempted
static void test_reserve_reports_size_overflow() {
    tda_Vec *v = make_vec(3);
    const void *before = tda_vec_data(v);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_reserve(v, SIZE_MAX));

    // the vec is left as it was
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    tda_vec_drop(v);
}

/* ========== shrink_to_fit ========== */

static void test_shrink_to_fit_drops_the_slack() {
    tda_Vec *v = make_vec_cap(16);
    push_int(v, 1);
    push_int(v, 2);
    push_int(v, 3);

    TDA_TEST_OK(tda_vec_shrink_to_fit(v));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(v));

    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 3);

    tda_vec_drop(v);
}

static void test_shrink_to_fit_when_already_exact_is_a_noop() {
    tda_Vec *v = make_vec(4);
    const void *before = tda_vec_data(v);

    TDA_TEST_OK(tda_vec_shrink_to_fit(v));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    tda_vec_drop(v);
}

// fitting nothing means owning nothing: the buffer is released, not resized to zero
static void test_shrink_to_fit_of_empty_releases_the_buffer() {
    tda_Vec *v = make_vec_cap(8);

    TDA_TEST_OK(tda_vec_shrink_to_fit(v));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(v));
    TEST_ASSERT_NULL(tda_vec_data(v));

    // and the vec stays usable afterwards
    push_int(v, 5);
    TEST_ASSERT_EQUAL_INT32(5, *TDA_VEC_GET_AS(int32_t, v, 0));

    tda_vec_drop(v);
}

static void test_shrink_to_fit_of_an_unallocated_vec_is_a_noop() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    TDA_TEST_OK(tda_vec_shrink_to_fit(v));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(v));
    TEST_ASSERT_NULL(tda_vec_data(v));

    tda_vec_drop(v);
}

/* ========== resize ========== */

static void test_resize_up_zeroes_the_tail() {
    tda_Vec *v = make_vec(2); // 0, 1

    TDA_TEST_OK(tda_vec_resize(v, 5));

    constexpr int32_t want[5] = {0, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_size_t(5, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 5);

    tda_vec_drop(v);
}

static void test_resize_down_keeps_the_head_and_the_capacity() {
    tda_Vec *v = make_vec(5);

    TDA_TEST_OK(tda_vec_resize(v, 2));

    constexpr int32_t want[2] = {0, 1};
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(5, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 2);

    tda_vec_drop(v);
}

// growing inside the existing room must not move the buffer, only zero the new tail
static void test_resize_within_the_capacity_does_not_reallocate() {
    tda_Vec *v = make_vec_cap(8);
    push_int(v, 1);
    push_int(v, 2);
    const void *before = tda_vec_data(v);

    TDA_TEST_OK(tda_vec_resize(v, 5));

    constexpr int32_t want[5] = {1, 2, 0, 0, 0};
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));
    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 5);

    tda_vec_drop(v);
}

static void test_resize_to_zero_keeps_the_buffer() {
    tda_Vec *v = make_vec(4);
    const void *before = tda_vec_data(v);

    TDA_TEST_OK(tda_vec_resize(v, 0));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    tda_vec_drop(v);
}

static void test_resize_to_the_same_len_is_a_noop() {
    tda_Vec *v = make_vec(3);

    TDA_TEST_OK(tda_vec_resize(v, 3));

    constexpr int32_t want[3] = {0, 1, 2};
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 3);

    tda_vec_drop(v);
}

static void test_resize_reports_size_overflow() {
    tda_Vec *v = make_vec(3);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_resize(v, SIZE_MAX));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(v));

    tda_vec_drop(v);
}

/* ========== swap ========== */

static void test_swap_self_is_noop() {
    tda_Vec *v = make_vec(3);
    const void *before = tda_vec_data(v);

    tda_vec_swap(v, v);

    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, v, 2));

    tda_vec_drop(v);
}

// one allocator on both sides: the buffers are handed over, never copied
static void test_swap_same_allocator_hands_over_buffers() {
    tda_Vec *a = make_vec(2);
    tda_Vec *b = make_vec(5);

    const void *pa = tda_vec_data(a);
    const void *pb = tda_vec_data(b);

    tda_vec_swap(a, b);

    TEST_ASSERT_EQUAL_PTR(pb, tda_vec_data(a));
    TEST_ASSERT_EQUAL_PTR(pa, tda_vec_data(b));
    TEST_ASSERT_EQUAL_size_t(5, tda_vec_len(a));
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(b));

    tda_vec_drop(b);
    tda_vec_drop(a);
}

// nothing is reallocated, so the spare capacity travels with its buffer
static void test_swap_same_allocator_carries_the_capacity() {
    tda_Vec *a = make_vec_cap(16);
    push_int(a, 1);

    tda_Vec *b = make_vec(3);

    tda_vec_swap(a, b);

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(a));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(a));
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(b));
    TEST_ASSERT_EQUAL_size_t(16, tda_vec_cap(b));

    tda_vec_drop(b);
    tda_vec_drop(a);
}

/* ========== swap_elems ========== */

static void test_swap_elems_exchanges_the_pair() {
    tda_Vec *v = make_vec(4);

    tda_vec_swap_elems(v, 0, 3);

    constexpr int32_t want[4] = {3, 1, 2, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 4);

    tda_vec_drop(v);
}

static void test_swap_elems_same_index_is_noop() {
    tda_Vec *v = make_vec(3);

    tda_vec_swap_elems(v, 1, 1);

    TEST_ASSERT_EQUAL_INT32(1, *TDA_VEC_GET_AS(int32_t, v, 1));

    tda_vec_drop(v);
}

// elem_size drives the swap, so a type wider than a word must move whole
static void test_swap_elems_moves_wide_elems_whole() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    tda_vec_swap_elems(v, 0, 1);

    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, v, 1)->b);

    tda_vec_drop(v);
}

/* ========== to span ========== */

// the view covers the content, not the allocation
static void test_to_span_follows_the_len_not_the_capacity() {
    tda_Vec *v = make_vec_cap(8);
    push_int(v, 1);
    push_int(v, 2);
    push_int(v, 3);

    const tda_Span s = tda_vec_to_span(v);

    TEST_ASSERT_EQUAL_PTR(tda_vec_data(v), s.data);
    TEST_ASSERT_EQUAL_size_t(3, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem_size);
    TEST_ASSERT_EQUAL_INT32(3, *TDA_SPAN_GET_AS(int32_t, s, 2));

    tda_vec_drop(v);
}

static void test_to_span_mut_writes_reach_the_vec() {
    tda_Vec *v = make_vec(4);

    const tda_SpanMut s = tda_vec_to_span_mut(v);
    TDA_SPAN_SET(int32_t, s, 0, 77);

    TEST_ASSERT_EQUAL_INT32(77, *TDA_VEC_GET_AS(int32_t, v, 0));

    tda_vec_drop(v);
}

static void test_to_span_of_empty_keeps_elem_size() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    const tda_Span s = tda_vec_to_span(v);

    TEST_ASSERT_NULL(s.data);
    TEST_ASSERT_EQUAL_size_t(0, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem_size);

    tda_vec_drop(v);
}

/* ========== allocation failure ========== */

// len * elem_size overflows size_t: reported, never attempted
static void test_new_len_reports_size_overflow() {
    tda_Vec *v = nullptr;

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_new_len(SIZE_MAX, 2, tda_al_default(), &v));

    TEST_ASSERT_NULL(v); // out is untouched on failure
}

static void test_new_cap_reports_size_overflow() {
    tda_Vec *v = nullptr;

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_new_cap(SIZE_MAX, 2, tda_al_default(), &v));

    TEST_ASSERT_NULL(v);
}

static void test_from_data_reports_size_overflow() {
    constexpr int32_t src[1] = {1};
    tda_Vec *v = nullptr;

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_from_data(src, SIZE_MAX, 2, tda_al_default(), &v));

    TEST_ASSERT_NULL(v);
}

static void test_new_len_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 64);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *v = nullptr;

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_VEC_NEW_LEN(int32_t, 1000, arena, &v));

    TEST_ASSERT_NULL(v);

    tda_al_arena_drop(arena);
}

// a push that cannot grow reports it and leaves the vec exactly as it was
static void test_push_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_CAP(int32_t, 2, arena, &v));

    push_int(v, 1);
    push_int(v, 2);
    const void *before = tda_vec_data(v);

    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_VEC_PUSH(int32_t, v, 3));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    constexpr int32_t want[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 2);

    tda_al_arena_drop(arena);
}

// when doubling does not fit, growth falls back to a single extra slot
static void test_push_falls_back_to_one_more_slot_when_doubling_fails() {
    const size_t for_three = arena_charge(3 * sizeof(Pair));
    const size_t for_four = arena_charge(4 * sizeof(Pair));
    if (for_three == for_four) {
        TEST_IGNORE_MESSAGE("the arena's alignment step cannot separate the two requests");
    }

    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_CAP(Pair, 2, arena, &v));

    TDA_TEST_OK(tda_vec_push(v, &(Pair){1, 2}));
    TDA_TEST_OK(tda_vec_push(v, &(Pair){3, 4}));

    // room for three elems, not for the four that doubling would ask for
    tda_test_arena_leave(arena, for_three);

    TDA_TEST_OK(tda_vec_push(v, &(Pair){5, 6}));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(v));

    // the elems moved with the buffer
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(5, TDA_VEC_GET_AS(Pair, v, 2)->a);
    TEST_ASSERT_EQUAL_INT64(6, TDA_VEC_GET_AS(Pair, v, 2)->b);

    tda_al_arena_drop(arena);
}

// a refused run must leave the vec exactly as it was: the length, the block and the
// elems, with no part of 'src' written anywhere
static void test_extend_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_CAP(int32_t, 2, arena, &v));
    push_int(v, 1);
    push_int(v, 2);
    const void *before = tda_vec_data(v);

    tda_test_arena_leave(arena, 0);

    constexpr int32_t src[3] = {7, 8, 9};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_extend(v, TDA_SPAN_FROM_DATA(int32_t, src, 3)));

    constexpr int32_t want[2] = {1, 2};
    assert_elems(v, want, 2);
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_cap(v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(v));

    tda_al_arena_drop(arena);
}

// the same for a run going into the middle: the tail must not be shifted by a call that
// then reports failure
static void test_insert_span_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_CAP(int32_t, 3, arena, &v));
    push_int(v, 1);
    push_int(v, 2);
    push_int(v, 3);

    tda_test_arena_leave(arena, 0);

    constexpr int32_t src[2] = {7, 8};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_insert_span(v, 1, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    constexpr int32_t want[3] = {1, 2, 3};
    assert_elems(v, want, 3);

    tda_al_arena_drop(arena);
}

// len + src.len overflows size_t: reported, and no byte is read from 'src'
static void test_extend_reports_size_overflow() {
    tda_Vec *v = make_vec(3);

    constexpr int32_t src[1] = {1};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_extend(v, TDA_SPAN_FROM_DATA(int32_t, src, SIZE_MAX)));

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(v, want, 3);

    tda_vec_drop(v);
}

// a run that fits in the room already there needs no allocator at all, so an exhausted
// arena is no obstacle
static void test_extend_within_the_capacity_needs_no_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW_CAP(int32_t, 8, arena, &v));
    push_int(v, 1);

    tda_test_arena_leave(arena, 0);

    constexpr int32_t src[3] = {7, 8, 9};
    TDA_TEST_OK(tda_vec_extend(v, TDA_SPAN_FROM_DATA(int32_t, src, 3)));

    constexpr int32_t want[4] = {1, 7, 8, 9};
    assert_elems(v, want, 4);

    tda_al_arena_drop(arena);
}

/* ========== macros ========== */

static void test_macro_of_builds_from_literals() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_al_default(), &v, 4, 5, 6));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_size(v));

    constexpr int32_t want[3] = {4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 3);

    tda_vec_drop(v);
}

static void test_macro_of_derives_len_from_the_list() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int64_t, tda_al_default(), &v, 1, 2, 3, 4, 5));

    TEST_ASSERT_EQUAL_size_t(5, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int64_t), tda_vec_elem_size(v));

    tda_vec_drop(v);
}

static void test_macro_push_appends_a_value() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    TDA_TEST_OK(TDA_VEC_PUSH(int32_t, v, 4));
    TDA_TEST_OK(TDA_VEC_PUSH(int32_t, v, 5));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    constexpr int32_t want[2] = {4, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 2);

    tda_vec_drop(v);
}

// the value lands in a compound literal, so it must be spelled out exactly once
static void test_macro_push_evaluates_its_value_once() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    int32_t next = 0;
    for (int i = 0; i < 3; ++i) {
        TDA_TEST_OK(TDA_VEC_PUSH(int32_t, v, next++));
    }

    TEST_ASSERT_EQUAL_INT32(3, next);
    constexpr int32_t want[3] = {0, 1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(v), 3);

    tda_vec_drop(v);
}

static void test_macro_from_data_infers_elem_size() {
    constexpr int32_t src[2] = {1, 2};

    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_FROM_DATA(int32_t, src, 2, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_size(v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_vec_data(v), 2);

    tda_vec_drop(v);
}

static void test_macro_extend_appends_a_literal_list() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_NEW(int32_t, tda_al_default(), &v));

    TDA_TEST_OK(TDA_VEC_EXTEND(int32_t, v, 4, 5, 6));

    constexpr int32_t want[3] = {4, 5, 6};
    assert_elems(v, want, 3);

    tda_vec_drop(v);
}

static void test_macro_insert_span_places_a_literal_list() {
    tda_Vec *v = make_vec(2); // 0, 1

    TDA_TEST_OK(TDA_VEC_INSERT_SPAN(int32_t, v, 1, 8, 9));

    constexpr int32_t want[4] = {0, 8, 9, 1};
    assert_elems(v, want, 4);

    tda_vec_drop(v);
}

/* ========== compare ========== */

static void test_eq_matches_the_same_elems() {
    tda_Vec *a = make_vec(4);
    tda_Vec *b = make_vec(4);

    TEST_ASSERT_TRUE(tda_vec_eq(a, a));
    TEST_ASSERT_TRUE(tda_vec_eq(a, b));
    TEST_ASSERT_TRUE(tda_vec_eq(b, a));
    TEST_ASSERT_TRUE(tda_vec_eq_by(a, b, tda_eq_i32));

    tda_vec_drop(a);
    tda_vec_drop(b);
}

static void test_eq_parts_one_differing_elem() {
    tda_Vec *a = make_vec(4);
    tda_Vec *b = make_vec(4);
    TDA_VEC_SET(int32_t, b, 3, 99);

    TEST_ASSERT_FALSE(tda_vec_eq(a, b));
    TEST_ASSERT_FALSE(tda_vec_eq_by(a, b, tda_eq_i32));

    tda_vec_drop(a);
    tda_vec_drop(b);
}

static void test_eq_parts_different_lengths() {
    tda_Vec *a = make_vec(4);
    tda_Vec *shorter = make_vec(3);

    TEST_ASSERT_FALSE(tda_vec_eq(a, shorter));
    TEST_ASSERT_FALSE(tda_vec_eq(shorter, a));

    tda_vec_drop(a);
    tda_vec_drop(shorter);
}

static void test_eq_of_two_empties() {
    tda_Vec *a = make_vec(0);
    tda_Vec *b = make_vec_cap(8);
    tda_Vec *one = make_vec(1);

    TEST_ASSERT_TRUE(tda_vec_eq(a, b));
    TEST_ASSERT_TRUE(tda_vec_eq_by(a, b, tda_eq_i32));
    TEST_ASSERT_FALSE(tda_vec_eq(a, one));

    tda_vec_drop(a);
    tda_vec_drop(b);
    tda_vec_drop(one);
}

// room is not contents: the two hold the same elems in blocks of different sizes
static void test_eq_ignores_capacity() {
    tda_Vec *a = make_vec(3);
    tda_Vec *b = make_vec_cap(64);
    for (int32_t i = 0; i < 3; ++i) {
        push_int(b, i);
    }

    TEST_ASSERT_TRUE(tda_vec_cap(a) != tda_vec_cap(b));
    TEST_ASSERT_TRUE(tda_vec_eq(a, b));
    TEST_ASSERT_TRUE(tda_vec_eq_by(a, b, tda_eq_i32));

    tda_vec_drop(a);
    tda_vec_drop(b);
}

// a popped elem is gone even though its bytes are still in the block
static void test_eq_forgets_a_popped_elem() {
    tda_Vec *a = make_vec(3);
    tda_Vec *b = make_vec(4);

    TEST_ASSERT_FALSE(tda_vec_eq(a, b));
    tda_vec_pop(b);
    TEST_ASSERT_TRUE(tda_vec_eq(a, b));

    tda_vec_drop(a);
    tda_vec_drop(b);
}

static void test_eq_by_asks_the_equality() {
    constexpr Pair lhs[2] = {{1, 10}, {2, 20}};
    constexpr Pair rhs[2] = {{1, 70}, {2, 80}};

    tda_Vec *a = nullptr;
    tda_Vec *b = nullptr;
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, lhs, 2, tda_al_default(), &a));
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, rhs, 2, tda_al_default(), &b));

    TEST_ASSERT_FALSE(tda_vec_eq(a, b));
    TEST_ASSERT_TRUE(tda_vec_eq_by(a, b, tda_test_pair_eq_a));

    tda_vec_drop(a);
    tda_vec_drop(b);
}

/* ========== print ========== */

// a printer writes to a stream, so a case reads one back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_Vec *v) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_vec_fprint(v, stream, tda_fprint_i32);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof buf - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_elems() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_al_default(), &v, 5, 3, 1));

    assert_prints("[5, 3, 1]\n", v);

    tda_vec_drop(v);
}

static void test_fprint_of_a_single_elem_has_no_separator() {
    tda_Vec *v = nullptr;
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_al_default(), &v, 7));

    assert_prints("[7]\n", v);

    tda_vec_drop(v);
}

static void test_fprint_of_an_empty_vec() {
    tda_Vec *v = make_vec(0);

    assert_prints("[]\n", v);

    tda_vec_drop(v);
}

// the stdout twin takes no stream and cannot be captured portably, so a case can only
// say that it runs and reaches the same printer
static void test_print_writes_to_stdout() {
    tda_Vec *v = make_vec(3);

    tda_vec_print(v, tda_fprint_i32);

    tda_vec_drop(v);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_starts_empty_and_unallocated);
    RUN_TEST(test_new_len_sets_len_cap_and_zeroes);
    RUN_TEST(test_new_cap_reserves_without_length);
    RUN_TEST(test_new_cap_zero_has_no_buffer);
    RUN_TEST(test_from_data_copies_and_detaches_the_source);
    RUN_TEST(test_from_data_empty_has_no_buffer);
    RUN_TEST(test_from_data_copies_whole_elems);
    RUN_TEST(test_from_span_copies_the_view);
    RUN_TEST(test_drop_null_is_noop);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_fits_the_len_not_the_source_capacity);
    RUN_TEST(test_copy_of_empty_has_no_buffer);
    RUN_TEST(test_copy_inherits_the_source_allocator);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_empties_the_source);
    RUN_TEST(test_move_assign_across_allocators_reports_an_exhausted_arena);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_assign_grows_and_shrinks_the_len);
    RUN_TEST(test_copy_assign_keeps_the_target_capacity);
    RUN_TEST(test_copy_assign_self_is_noop);
    RUN_TEST(test_copy_assign_keeps_the_target_allocator);

    RUN_TEST(test_bytes_is_len_times_elem_size);
    RUN_TEST(test_bytes_ignores_the_spare_capacity);
    RUN_TEST(test_bytes_of_empty_is_zero);
    RUN_TEST(test_bytes_tracks_elem_size);
    RUN_TEST(test_bytes_agrees_with_the_span);

    RUN_TEST(test_set_get_roundtrip);
    RUN_TEST(test_get_mut_writes_through);
    RUN_TEST(test_first_and_last_address_the_ends);
    RUN_TEST(test_first_and_last_mut_write_through);
    RUN_TEST(test_first_and_last_coincide_on_a_single_elem);
    RUN_TEST(test_last_follows_the_len);
    RUN_TEST(test_data_mut_writes_through);

    RUN_TEST(test_push_appends_and_doubles_the_capacity);
    RUN_TEST(test_push_uses_reserved_capacity_without_reallocating);
    RUN_TEST(test_push_moves_whole_elems);
    RUN_TEST(test_pop_shortens_and_keeps_the_capacity);
    RUN_TEST(test_push_after_pop_overwrites_the_slot);

    RUN_TEST(test_insert_at_front_middle_and_end);
    RUN_TEST(test_insert_into_an_empty_vec);
    RUN_TEST(test_insert_grows_when_full);
    RUN_TEST(test_remove_from_front_middle_and_end);
    RUN_TEST(test_remove_of_the_last_elem_empties_the_vec);
    RUN_TEST(test_insert_and_remove_move_whole_elems);

    RUN_TEST(test_extend_appends_in_order);
    RUN_TEST(test_extend_onto_an_empty_vec);
    RUN_TEST(test_extend_with_an_empty_span_changes_nothing);
    RUN_TEST(test_extend_takes_the_room_once);
    RUN_TEST(test_a_run_of_extends_stays_amortized);
    RUN_TEST(test_a_run_of_resizes_stays_amortized);
    RUN_TEST(test_growing_alone_on_an_arena_costs_only_the_capacity);
    RUN_TEST(test_extend_moves_wide_elems_whole);

    RUN_TEST(test_insert_span_puts_the_run_before_the_index);
    RUN_TEST(test_insert_span_at_the_front);
    RUN_TEST(test_insert_span_at_len_is_extend);
    RUN_TEST(test_insert_span_of_an_empty_span_changes_nothing);
    RUN_TEST(test_insert_span_keeps_a_long_tail_in_order);

    RUN_TEST(test_remove_range_closes_the_gap);
    RUN_TEST(test_remove_range_at_the_front);
    RUN_TEST(test_remove_range_to_the_end);
    RUN_TEST(test_remove_range_of_zero_changes_nothing);
    RUN_TEST(test_remove_range_of_everything_empties_the_vec);
    RUN_TEST(test_remove_range_leaves_the_capacity_alone);
    RUN_TEST(test_remove_range_moves_wide_elems_whole);

    RUN_TEST(test_clear_drops_the_len_and_keeps_the_buffer);
    RUN_TEST(test_clear_of_an_empty_vec_is_a_noop);

    RUN_TEST(test_reserve_grows_the_capacity_and_keeps_the_contents);
    RUN_TEST(test_reserve_below_the_capacity_is_a_noop);
    RUN_TEST(test_reserve_reports_size_overflow);

    RUN_TEST(test_shrink_to_fit_drops_the_slack);
    RUN_TEST(test_shrink_to_fit_when_already_exact_is_a_noop);
    RUN_TEST(test_shrink_to_fit_of_empty_releases_the_buffer);
    RUN_TEST(test_shrink_to_fit_of_an_unallocated_vec_is_a_noop);

    RUN_TEST(test_resize_up_zeroes_the_tail);
    RUN_TEST(test_resize_down_keeps_the_head_and_the_capacity);
    RUN_TEST(test_resize_within_the_capacity_does_not_reallocate);
    RUN_TEST(test_resize_to_zero_keeps_the_buffer);
    RUN_TEST(test_resize_to_the_same_len_is_a_noop);
    RUN_TEST(test_resize_reports_size_overflow);

    RUN_TEST(test_swap_self_is_noop);
    RUN_TEST(test_swap_same_allocator_hands_over_buffers);
    RUN_TEST(test_swap_same_allocator_carries_the_capacity);

    RUN_TEST(test_swap_elems_exchanges_the_pair);
    RUN_TEST(test_swap_elems_same_index_is_noop);
    RUN_TEST(test_swap_elems_moves_wide_elems_whole);

    RUN_TEST(test_to_span_follows_the_len_not_the_capacity);
    RUN_TEST(test_to_span_mut_writes_reach_the_vec);
    RUN_TEST(test_to_span_of_empty_keeps_elem_size);

    RUN_TEST(test_new_len_reports_size_overflow);
    RUN_TEST(test_new_cap_reports_size_overflow);
    RUN_TEST(test_from_data_reports_size_overflow);
    RUN_TEST(test_new_len_reports_an_exhausted_arena);
    RUN_TEST(test_push_reports_an_exhausted_arena);
    RUN_TEST(test_push_falls_back_to_one_more_slot_when_doubling_fails);
    RUN_TEST(test_extend_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_insert_span_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_extend_reports_size_overflow);
    RUN_TEST(test_extend_within_the_capacity_needs_no_allocator);

    RUN_TEST(test_macro_of_builds_from_literals);
    RUN_TEST(test_macro_of_derives_len_from_the_list);
    RUN_TEST(test_macro_push_appends_a_value);
    RUN_TEST(test_macro_push_evaluates_its_value_once);
    RUN_TEST(test_macro_from_data_infers_elem_size);
    RUN_TEST(test_macro_extend_appends_a_literal_list);
    RUN_TEST(test_macro_insert_span_places_a_literal_list);


    RUN_TEST(test_eq_matches_the_same_elems);
    RUN_TEST(test_eq_parts_one_differing_elem);
    RUN_TEST(test_eq_parts_different_lengths);
    RUN_TEST(test_eq_of_two_empties);
    RUN_TEST(test_eq_ignores_capacity);
    RUN_TEST(test_eq_forgets_a_popped_elem);
    RUN_TEST(test_eq_by_asks_the_equality);

    RUN_TEST(test_fprint_writes_the_elems);
    RUN_TEST(test_fprint_of_a_single_elem_has_no_separator);
    RUN_TEST(test_fprint_of_an_empty_vec);
    RUN_TEST(test_print_writes_to_stdout);

    return UNITY_END();
}
