#include "tda/ds/vec.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/print.h"

#include "support/arena.h"
#include "support/i32.h"
#include "support/pair.h"
#include "support/probe.h"
#include "support/status.h"

#include "support/print.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// int32_t vec of len elems holding 0, 1, ... len-1; cap == len
static tda_Vec make_vec(size_t len) {
    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_LEN(int32_t, len, tda_al_default(), &v));

    for (size_t i = 0; i < len; ++i) {
        TDA_VEC_SET(int32_t, &v, i, (int32_t) i);
    }

    return v;
}

// empty int32_t vec with room for cap elems
static tda_Vec make_vec_cap(size_t cap) {
    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_CAP(int32_t, cap, tda_al_default(), &v));

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

static void test_init_starts_empty_and_unallocated() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_layout(&v).size);
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_vec_al(&v)));
    TEST_ASSERT_NULL(tda_vec_data(&v));

    tda_vec_deinit(&v);
}

static void test_init_len_sets_len_cap_and_zeroes() {
    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_LEN(int32_t, 4, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(&v));

    constexpr int32_t zeroes[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(zeroes, tda_vec_data(&v), 4);

    tda_vec_deinit(&v);
}

// cap is room, not content, so the vec is still empty
static void test_init_cap_reserves_without_length() {
    tda_Vec v = make_vec_cap(8);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(&v));
    TEST_ASSERT_NOT_NULL(tda_vec_data(&v));

    tda_vec_deinit(&v);
}

static void test_init_cap_zero_has_no_buffer() {
    tda_Vec v = make_vec_cap(0);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(&v));
    TEST_ASSERT_NULL(tda_vec_data(&v));

    tda_vec_deinit(&v);
}

static void test_from_data_copies_and_detaches_the_source() {
    int32_t src[3] = {5, 6, 7};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(int32_t, src, 3, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_vec_data(&v), 3);
    TEST_ASSERT_TRUE((const void *) src != tda_vec_data(&v));

    src[0] = 999;
    TEST_ASSERT_EQUAL_INT32(5, *TDA_VEC_GET_AS(int32_t, &v, 0));

    tda_vec_deinit(&v);
}

// a null source is legal while len == 0, as in tda_span_from_data
static void test_from_data_empty_has_no_buffer() {
    tda_Vec v = {};
    TDA_TEST_OK(tda_vec_from_data(nullptr, 0, TDA_LAYOUT_OF(int32_t), tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(&v));
    TEST_ASSERT_NULL(tda_vec_data(&v));

    tda_vec_deinit(&v);
}

// elem_size drives the copy, so a type wider than a word must arrive whole
static void test_from_data_copies_whole_elems() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_vec_elem_layout(&v).size);
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, &v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, &v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, &v, 1)->b);

    tda_vec_deinit(&v);
}

static void test_from_span_copies_the_view() {
    constexpr int32_t src[3] = {7, 8, 9};

    tda_Vec v = {};
    TDA_TEST_OK(tda_vec_from_span(TDA_SPAN_FROM_DATA(int32_t, src, 3), tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_layout(&v).size);
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_vec_data(&v), 3);
    TEST_ASSERT_TRUE((const void *) src != tda_vec_data(&v));

    tda_vec_deinit(&v);
}

static void test_deinit_of_an_inert_value_is_noop() {
    tda_Vec v = {};
    tda_vec_deinit(&v);
    tda_vec_deinit(&v);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_Vec v = make_vec(3);

    tda_vec_deinit(&v);
    TEST_ASSERT_NULL(v.data_);
    TEST_ASSERT_NULL(v.al_.vt);

    tda_vec_deinit(&v);
}

static void test_init_allocates_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec v = TDA_VEC_INIT(int32_t, al);

    TEST_ASSERT_EQUAL_size_t(0, tda_test_probe_requests(&probe));
    TEST_ASSERT_TRUE(tda_al_eq(al, tda_vec_al(&v)));

    tda_vec_deinit(&v);
    TEST_ASSERT_EQUAL_size_t(0, tda_test_probe_requests(&probe));
}

// a vec inside a struct of the caller's, with no allocation for the vec itself
static void test_a_vec_lives_inside_a_struct() {
    struct Node {
        int32_t id;
        tda_Vec kids;
    } node = {.id = 7};
    node.kids = TDA_VEC_INIT(int32_t, tda_al_default());

    TDA_TEST_OK(TDA_VEC_PUSH(int32_t, &node.kids, 1));
    TDA_TEST_OK(TDA_VEC_PUSH(int32_t, &node.kids, 2));
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&node.kids));

    tda_vec_deinit(&node.kids);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_Vec src = make_vec(4);

    tda_Vec dst = {};
    TDA_TEST_OK(tda_vec_copy(&src, &dst));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(&src), tda_vec_data(&dst), 4);
    TEST_ASSERT_TRUE(tda_vec_data(&src) != tda_vec_data(&dst));

    TDA_VEC_SET(int32_t, &src, 0, 999);
    TEST_ASSERT_EQUAL_INT32(0, *TDA_VEC_GET_AS(int32_t, &dst, 0));

    tda_vec_deinit(&dst);
    tda_vec_deinit(&src);
}

// the copy is sized to the content; the source's spare capacity is not carried over
static void test_copy_fits_the_len_not_the_source_capacity() {
    tda_Vec src = make_vec_cap(8);
    push_int(&src, 1);
    push_int(&src, 2);

    tda_Vec dst = {};
    TDA_TEST_OK(tda_vec_copy(&src, &dst));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_cap(&dst));

    tda_vec_deinit(&dst);
    tda_vec_deinit(&src);
}

static void test_copy_of_empty_has_no_buffer() {
    tda_Vec src = make_vec(0);

    tda_Vec dst = {};
    TDA_TEST_OK(tda_vec_copy(&src, &dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_layout(&dst).size);
    TEST_ASSERT_NULL(tda_vec_data(&dst));

    tda_vec_deinit(&dst);
    tda_vec_deinit(&src);
}

static void test_copy_inherits_the_source_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec src = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_arena_to_al(&arena), &src, 1, 2, 3));

    tda_Vec dst = {};
    TDA_TEST_OK(tda_vec_copy(&src, &dst));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_vec_al(&dst)));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(&src), tda_vec_data(&dst), 3);

    tda_vec_deinit(&dst);
    tda_vec_deinit(&src);
    tda_arena_deinit(&arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec src = make_vec(4);

    tda_Vec dst = {};
    TDA_TEST_OK(tda_vec_copy_with(&src, tda_arena_to_al(&arena), &dst));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_vec_al(&dst)));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_vec_al(&src)));
    TEST_ASSERT_TRUE(tda_vec_eq(&src, &dst));

    // the copy outlives the source, so it owns its elems
    tda_vec_deinit(&src);
    TEST_ASSERT_EQUAL_INT32(3, *TDA_VEC_GET_AS(int32_t, &dst, 3));

    tda_vec_deinit(&dst);
    tda_arena_deinit(&arena);
}

// the blocks come from the copy's allocator, not the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));
    tda_test_arena_leave(&arena, 0);

    tda_Vec src = make_vec(4);

    tda_Vec dst;
    TDA_TEST_POISON(dst);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_copy_with(&src, tda_arena_to_al(&arena), &dst));
    TDA_TEST_ASSERT_POISONED(dst);
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(&src));

    tda_vec_deinit(&src);
    tda_arena_deinit(&arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec src = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, al, &src, 1, 2, 3));

    tda_Vec dst = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, al, &dst, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    tda_vec_move_assign(&dst, &src);

    // no allocation: the block is handed over with its capacity
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, &dst, 1));

    // the source is left empty, blockless and usable
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&src));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(&src));
    TEST_ASSERT_NULL(tda_vec_data(&src));

    tda_vec_deinit(&src);
    tda_vec_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the allocator is part of the value: 'dst' takes the block with the allocator it came from
static void test_move_assign_across_allocators_hands_the_allocator_over() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec src = make_vec(4);
    const void *block = tda_vec_data(&src);

    tda_Vec dst = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_arena_to_al(&arena), &dst, 9));

    tda_vec_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_PTR(block, tda_vec_data(&dst));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_vec_al(&dst)));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_VEC_GET_AS(int32_t, &dst, 3));

    // the source is left empty and usable, on the allocator it had
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&src));
    TEST_ASSERT_NULL(tda_vec_data(&src));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_vec_al(&src)));
    push_int(&src, 5);

    tda_vec_deinit(&src);
    tda_vec_deinit(&dst);
    tda_arena_deinit(&arena);
}

// nothing is allocated, so an exhausted allocator on either side cannot stop it
static void test_move_assign_needs_no_memory() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec dst = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_arena_to_al(&arena), &dst, 9));
    tda_test_arena_leave(&arena, 0);

    tda_Vec src = make_vec(4);

    tda_vec_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&src));

    tda_vec_deinit(&src);
    tda_vec_deinit(&dst);
    tda_arena_deinit(&arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_Vec v = make_vec(3);

    tda_vec_move_assign(&v, &v);

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, &v, 2));

    tda_vec_deinit(&v);
}

static void test_copy_assign_grows_and_shrinks_the_len() {
    tda_Vec src = make_vec(6), dst = make_vec(2);

    // grow: 2 -> 6
    TDA_TEST_OK(tda_vec_copy_assign(&dst, &src));
    TEST_ASSERT_EQUAL_size_t(6, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(&src), tda_vec_data(&dst), 6);

    // shrink: 6 -> 3
    tda_Vec small = make_vec(3);
    TDA_TEST_OK(tda_vec_copy_assign(&dst, &small));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(&small), tda_vec_data(&dst), 3);

    // shrink to empty: the len goes, the buffer stays
    tda_Vec empty = make_vec(0);
    TDA_TEST_OK(tda_vec_copy_assign(&dst, &empty));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&dst));
    TEST_ASSERT_NOT_NULL(tda_vec_data(&dst));

    tda_vec_deinit(&empty);
    tda_vec_deinit(&small);
    tda_vec_deinit(&dst);
    tda_vec_deinit(&src);
}

// unlike arr, assignment never gives capacity back; shrink_to_fit does that
static void test_copy_assign_keeps_the_target_capacity() {
    tda_Vec src = make_vec(2), dst = make_vec(6);

    TDA_TEST_OK(tda_vec_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&dst));
    TEST_ASSERT_EQUAL_size_t(6, tda_vec_cap(&dst));

    tda_vec_deinit(&dst);
    tda_vec_deinit(&src);
}

static void test_copy_assign_of_itself_changes_nothing() {
    tda_Vec v = make_vec(3);

    TDA_TEST_OK(tda_vec_copy_assign(&v, &v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, &v, 2));

    tda_vec_deinit(&v);
}

// assignment resizes through the target's allocator, not the source's
static void test_copy_assign_keeps_the_target_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec src = make_vec(4);

    tda_Vec dst = {};
    TDA_TEST_OK(TDA_VEC_INIT_LEN(int32_t, 1, tda_arena_to_al(&arena), &dst));

    TDA_TEST_OK(tda_vec_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(&dst));
    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_vec_al(&dst)));
    TEST_ASSERT_EQUAL_INT32_ARRAY(tda_vec_data(&src), tda_vec_data(&dst), 4);

    tda_vec_deinit(&dst);
    tda_vec_deinit(&src);
    tda_arena_deinit(&arena);
}

/* ========== info ========== */

static void test_bytes_is_len_times_elem_size() {
    tda_Vec v = make_vec(4);

    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), tda_vec_bytes(&v));

    tda_vec_deinit(&v);
}

// bytes measures the content; spare capacity does not count
static void test_bytes_ignores_the_spare_capacity() {
    tda_Vec v = make_vec_cap(8);
    push_int(&v, 1);
    push_int(&v, 2);
    push_int(&v, 3);

    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_size_t(3 * sizeof(int32_t), tda_vec_bytes(&v));

    tda_vec_deinit(&v);
}

static void test_bytes_of_empty_is_zero() {
    tda_Vec v = make_vec_cap(8);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_bytes(&v));

    tda_vec_deinit(&v);
}

// elem_size, not the elem count, drives the total
static void test_bytes_tracks_elem_size() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(2 * sizeof(Pair), tda_vec_bytes(&v));

    tda_vec_deinit(&v);
}

// the vec and its view must agree on the size of the same memory
static void test_bytes_agrees_with_the_span() {
    tda_Vec v = make_vec_cap(8);
    push_int(&v, 1);
    push_int(&v, 2);

    TEST_ASSERT_EQUAL_size_t(tda_span_bytes(tda_vec_to_span(&v)), tda_vec_bytes(&v));

    tda_vec_deinit(&v);
}

/* ========== access ========== */

static void test_set_get_roundtrip() {
    tda_Vec v = make_vec(5);

    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_INT32((int32_t) i, *TDA_VEC_GET_AS(int32_t, &v, i));
    }

    tda_vec_deinit(&v);
}

// the value may alias the elem it is set over
static void test_set_from_its_own_elem() {
    tda_Vec v = make_vec(3);

    tda_vec_set(&v, 1, tda_vec_get(&v, 1));
    tda_vec_set(&v, 0, tda_vec_get(&v, 2));

    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, &v, 0));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_VEC_GET_AS(int32_t, &v, 1));

    tda_vec_deinit(&v);
}

static void test_get_mut_writes_through() {
    tda_Vec v = make_vec(3);

    *TDA_VEC_GET_MUT_AS(int32_t, &v, 1) = 42;
    TEST_ASSERT_EQUAL_INT32(42, *TDA_VEC_GET_AS(int32_t, &v, 1));

    tda_vec_deinit(&v);
}

static void test_first_and_last_address_the_ends() {
    tda_Vec v = make_vec(4);

    TEST_ASSERT_EQUAL_INT32(0, *TDA_VEC_FRONT_AS(int32_t, &v));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_VEC_BACK_AS(int32_t, &v));
    TEST_ASSERT_EQUAL_PTR(tda_vec_data(&v), tda_vec_front(&v));
    TEST_ASSERT_EQUAL_PTR(TDA_VEC_GET_AS(int32_t, &v, 3), tda_vec_back(&v));

    tda_vec_deinit(&v);
}

static void test_first_and_last_mut_write_through() {
    tda_Vec v = make_vec(4);

    *TDA_VEC_FRONT_MUT_AS(int32_t, &v) = 10;
    *TDA_VEC_BACK_MUT_AS(int32_t, &v) = 20;

    TEST_ASSERT_EQUAL_INT32(10, *TDA_VEC_GET_AS(int32_t, &v, 0));
    TEST_ASSERT_EQUAL_INT32(20, *TDA_VEC_GET_AS(int32_t, &v, 3));

    tda_vec_deinit(&v);
}

static void test_first_and_last_coincide_on_a_single_elem() {
    tda_Vec v = make_vec(1);

    TEST_ASSERT_EQUAL_PTR(tda_vec_front(&v), tda_vec_back(&v));
    TEST_ASSERT_EQUAL_PTR(tda_vec_front_mut(&v), tda_vec_back_mut(&v));

    tda_vec_deinit(&v);
}

// last follows the len, not the capacity
static void test_last_follows_the_len() {
    tda_Vec v = make_vec_cap(8);
    push_int(&v, 1);
    push_int(&v, 2);

    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_BACK_AS(int32_t, &v));
    TEST_ASSERT_EQUAL_PTR(TDA_VEC_GET_AS(int32_t, &v, 1), tda_vec_back(&v));

    tda_vec_deinit(&v);
}

static void test_data_mut_writes_through() {
    tda_Vec v = make_vec(3);

    int32_t *d = tda_vec_data_mut(&v);
    d[2] = 99;

    TEST_ASSERT_EQUAL_INT32(99, *TDA_VEC_GET_AS(int32_t, &v, 2));

    tda_vec_deinit(&v);
}

/* ========== push / pop ========== */

// growth is 0 -> 1 and then doubling, and it only fires when the vec is full
static void test_push_appends_and_doubles_the_capacity() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    constexpr size_t want_cap[5] = {1, 2, 4, 4, 8};
    for (size_t i = 0; i < 5; ++i) {
        push_int(&v, (int32_t) i);

        TEST_ASSERT_EQUAL_size_t(i + 1, tda_vec_len(&v));
        TEST_ASSERT_EQUAL_size_t(want_cap[i], tda_vec_cap(&v));
    }

    constexpr int32_t want[5] = {0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 5);

    tda_vec_deinit(&v);
}

// reserved room is used as is: no growth, no move
static void test_push_uses_reserved_capacity_without_reallocating() {
    tda_Vec v = make_vec_cap(4);
    const void *before = tda_vec_data(&v);

    for (int32_t i = 0; i < 4; ++i) {
        push_int(&v, i);
    }

    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(&v));

    tda_vec_deinit(&v);
}

// one reserve up front, and the pushes after it neither allocate nor report
static void test_push_assume_cap_after_a_reserve_allocates_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec v = TDA_VEC_INIT(int32_t, al);
    TDA_TEST_OK(tda_vec_reserve(&v, 8));
    const size_t requests = tda_test_probe_requests(&probe);

    for (int32_t i = 0; i < 8; ++i) {
        TDA_VEC_PUSH_ASSUME_CAP(int32_t, &v, i * 10);
    }

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_size_t(8, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32(70, *TDA_VEC_BACK_AS(int32_t, &v));

    tda_vec_deinit(&v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// nothing moves, so an elem of the vec itself may be pushed again
static void test_push_assume_cap_takes_an_elem_of_its_own() {
    tda_Vec v = make_vec_cap(4);
    push_int(&v, 7);

    tda_vec_push_assume_cap(&v, tda_vec_get(&v, 0));

    constexpr int32_t want[2] = {7, 7};
    assert_elems(&v, want, 2);

    tda_vec_deinit(&v);
}

// the slot is the new back, filled in place
static void test_add_one_hands_out_the_new_back() {
    tda_Vec v = TDA_VEC_INIT(Pair, tda_al_default());

    for (int64_t i = 0; i < 5; ++i) {
        void *slot;
        TDA_TEST_OK(tda_vec_add_one(&v, &slot));
        TEST_ASSERT_EQUAL_PTR(tda_vec_back(&v), slot);
        *(Pair *) slot = (Pair){.a = i, .b = -i};
    }

    TEST_ASSERT_EQUAL_size_t(5, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 3)->a);
    TEST_ASSERT_EQUAL_INT64(-4, TDA_VEC_GET_AS(Pair, &v, 4)->b);

    tda_vec_deinit(&v);
}

// a refused growth leaves the vec as it was and the out untouched
static void test_add_one_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_CAP(int32_t, 1, tda_arena_to_al(&arena), &v));
    push_int(&v, 1);
    tda_test_arena_leave(&arena, 0);

    void *slot = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_add_one(&v, &slot));

    TEST_ASSERT_NULL(slot);
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_cap(&v));

    tda_vec_deinit(&v);
    tda_arena_deinit(&arena);
}

static void test_add_one_assume_cap_allocates_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec v;
    TDA_TEST_OK(TDA_VEC_INIT_CAP(Pair, 3, al, &v));
    const size_t requests = tda_test_probe_requests(&probe);

    for (int64_t i = 0; i < 3; ++i) {
        *TDA_VEC_ADD_ONE_ASSUME_CAP_AS(Pair, &v) = (Pair){.a = i, .b = i * i};
    }

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_BACK_AS(Pair, &v)->b);

    tda_vec_deinit(&v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_push_moves_whole_elems() {
    tda_Vec v = TDA_VEC_INIT(Pair, tda_al_default());

    TDA_TEST_OK(tda_vec_push(&v, &(Pair){1, 2}));
    TDA_TEST_OK(tda_vec_push(&v, &(Pair){3, 4}));

    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, &v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, &v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, &v, 1)->b);

    tda_vec_deinit(&v);
}

static void test_pop_shortens_and_keeps_the_capacity() {
    tda_Vec v = make_vec(3);
    const size_t cap = tda_vec_cap(&v);

    tda_vec_pop(&v);

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_VEC_BACK_AS(int32_t, &v));

    tda_vec_pop(&v);
    tda_vec_pop(&v);
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

// the slot a pop released is the one the next push takes
static void test_push_after_pop_overwrites_the_slot() {
    tda_Vec v = make_vec(3);
    const void *before = tda_vec_data(&v);

    tda_vec_pop(&v);
    push_int(&v, 77);

    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32(77, *TDA_VEC_GET_AS(int32_t, &v, 2));

    tda_vec_deinit(&v);
}

static void test_pop_to_hands_the_back_elem_out() {
    tda_Vec v = make_vec(3);
    const size_t cap = tda_vec_cap(&v);

    int32_t out = -1;
    tda_vec_pop_to(&v, &out);

    TEST_ASSERT_EQUAL_INT32(2, out);
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(&v));

    tda_vec_pop_to(&v, &out);
    tda_vec_pop_to(&v, &out);

    TEST_ASSERT_EQUAL_INT32(0, out);
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

// 'out' may be any slot of the block, the one just dropped included
static void test_pop_to_into_the_vec_itself() {
    tda_Vec v = make_vec(3);

    tda_vec_pop_to(&v, TDA_VEC_GET_MUT_AS(int32_t, &v, 0));

    constexpr int32_t want[2] = {2, 1};
    assert_elems(&v, want, 2);

    int32_t *back = TDA_VEC_BACK_MUT_AS(int32_t, &v);
    tda_vec_pop_to(&v, back);

    TEST_ASSERT_EQUAL_INT32(1, *back);
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

static void test_pop_to_moves_whole_elems() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    Pair out = {0, 0};
    TDA_VEC_POP_TO(Pair, &v, &out);

    TEST_ASSERT_EQUAL_INT64(3, out.a);
    TEST_ASSERT_EQUAL_INT64(4, out.b);
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

/* ========== insert / remove ========== */

static void test_insert_at_front_middle_and_end() {
    tda_Vec v = make_vec(3); // 0, 1, 2

    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, &v, 0, 10));
    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, &v, 2, 20));
    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, &v, 5, 30));

    constexpr int32_t want[6] = {10, 0, 20, 1, 2, 30};
    TEST_ASSERT_EQUAL_size_t(6, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 6);

    tda_vec_deinit(&v);
}

// idx == len on an empty vec is the only legal index there
static void test_insert_into_an_empty_vec() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, &v, 0, 42));

    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_INT32(42, *TDA_VEC_GET_AS(int32_t, &v, 0));

    tda_vec_deinit(&v);
}

// a full vec must grow before the tail shifts, or the shift writes past the buffer
static void test_insert_grows_when_full() {
    tda_Vec v = make_vec(2); // len == cap == 2

    TDA_TEST_OK(TDA_VEC_INSERT(int32_t, &v, 0, 9));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_TRUE(tda_vec_cap(&v) >= 3);

    constexpr int32_t want[3] = {9, 0, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 3);

    tda_vec_deinit(&v);
}

static void test_remove_from_front_middle_and_end() {
    tda_Vec v = make_vec(5); // 0, 1, 2, 3, 4

    tda_vec_remove(&v, 0); // 1, 2, 3, 4
    tda_vec_remove(&v, 1); // 1, 3, 4
    tda_vec_remove(&v, 2); // 1, 3

    constexpr int32_t want[2] = {1, 3};
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 2);

    tda_vec_deinit(&v);
}

static void test_remove_of_the_last_elem_empties_the_vec() {
    tda_Vec v = make_vec(1);

    tda_vec_remove(&v, 0);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_cap(&v));

    tda_vec_deinit(&v);
}

// the tail shift is sized in bytes, so wide elems must travel whole
static void test_insert_and_remove_move_whole_elems() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    TDA_TEST_OK(tda_vec_insert(&v, 1, &(Pair){5, 6}));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT64(5, TDA_VEC_GET_AS(Pair, &v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(6, TDA_VEC_GET_AS(Pair, &v, 1)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 2)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, &v, 2)->b);

    tda_vec_remove(&v, 0);

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT64(5, TDA_VEC_GET_AS(Pair, &v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(6, TDA_VEC_GET_AS(Pair, &v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, &v, 1)->b);

    tda_vec_deinit(&v);
}

static void test_swap_remove_fills_the_gap_with_the_back() {
    tda_Vec v = make_vec(5); // 0, 1, 2, 3, 4
    const size_t cap = tda_vec_cap(&v);

    tda_vec_swap_remove(&v, 1); // 0, 4, 2, 3
    tda_vec_swap_remove(&v, 0); // 3, 4, 2

    constexpr int32_t want[3] = {3, 4, 2};
    assert_elems(&v, want, 3);
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(&v));

    tda_vec_deinit(&v);
}

// the back elem has no gap to fill: it is dropped and nothing moves
static void test_swap_remove_of_the_back_elem() {
    tda_Vec v = make_vec(3);

    tda_vec_swap_remove(&v, 2);

    constexpr int32_t want[2] = {0, 1};
    assert_elems(&v, want, 2);

    tda_vec_swap_remove(&v, 1);
    tda_vec_swap_remove(&v, 0);
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

static void test_swap_remove_moves_whole_elems() {
    constexpr Pair src[3] = {{1, 10}, {2, 20}, {3, 30}};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 3, tda_al_default(), &v));

    tda_vec_swap_remove(&v, 0);

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(30, TDA_VEC_GET_AS(Pair, &v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, &v, 1)->a);

    tda_vec_deinit(&v);
}

/* ========== bulk mods ========== */

static void test_extend_appends_in_order() {
    tda_Vec v = make_vec(3); // 0, 1, 2
    constexpr int32_t src[3] = {7, 8, 9};

    TDA_TEST_OK(tda_vec_extend(&v, TDA_SPAN_FROM_DATA(int32_t, src, 3)));

    constexpr int32_t want[6] = {0, 1, 2, 7, 8, 9};
    assert_elems(&v, want, 6);

    tda_vec_deinit(&v);
}

static void test_extend_onto_an_empty_vec() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    constexpr int32_t src[2] = {4, 5};
    TDA_TEST_OK(tda_vec_extend(&v, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    assert_elems(&v, src, 2);

    tda_vec_deinit(&v);
}

// an empty run is valid: nothing is allocated and nothing moves
static void test_extend_with_an_empty_span_changes_nothing() {
    tda_Vec v = make_vec(3);
    const size_t cap = tda_vec_cap(&v);
    const void *before = tda_vec_data(&v);

    TDA_TEST_OK(tda_vec_extend(&v, TDA_SPAN_FROM_DATA(int32_t, nullptr, 0)));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_vec_deinit(&v);
}

// the room for the run is requested ONCE; the same pushes would request it several times
// as the vec grows
static void test_extend_takes_the_room_once() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec v = TDA_VEC_INIT(int32_t, al);

    const size_t before = tda_test_probe_requests(&probe);

    constexpr int32_t src[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    TDA_TEST_OK(tda_vec_extend(&v, TDA_SPAN_FROM_DATA(int32_t, src, 8)));

    TEST_ASSERT_EQUAL_size_t(1, tda_test_probe_requests(&probe) - before);
    assert_elems(&v, src, 8);

    tda_vec_deinit(&v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// one-elem extends must grow by the growth factor like pushes, not allocate on every call
static void test_a_run_of_extends_stays_amortized() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec v = TDA_VEC_INIT(int32_t, al);

    for (int32_t i = 0; i < 64; ++i) {
        TDA_TEST_OK(tda_vec_extend(&v, TDA_SPAN_FROM_DATA(int32_t, &i, 1)));
    }

    TEST_ASSERT_EQUAL_size_t(64, tda_vec_len(&v));
    TEST_ASSERT_TRUE(tda_test_probe_requests(&probe) < 20);
    TEST_ASSERT_EQUAL_INT32(63, *TDA_VEC_GET_AS(int32_t, &v, 63));

    tda_vec_deinit(&v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// resize(len + 1) grows by one elem to fill in place, so a run of them must cost what
// pushes cost, not one allocation per call
static void test_a_run_of_resizes_stays_amortized() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Vec v = TDA_VEC_INIT(int32_t, al);

    for (size_t len = 1; len <= 64; ++len) {
        TDA_TEST_OK(tda_vec_resize(&v, len));
    }

    TEST_ASSERT_EQUAL_size_t(64, tda_vec_len(&v));
    TEST_ASSERT_TRUE(tda_test_probe_requests(&probe) < 20);

    tda_vec_deinit(&v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// alone on an arena the block is always the last one and grows in place, so the arena is
// charged the final capacity, not every capacity along the way
static void test_growing_alone_on_an_arena_costs_only_the_capacity() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1 << 16, tda_al_default(), &arena));

    tda_Vec v = TDA_VEC_INIT(int32_t, tda_arena_to_al(&arena));

    // counted from the first buffer, so the padding before it stays out
    constexpr int32_t zero = 0;
    TDA_TEST_OK(tda_vec_push(&v, &zero));
    const size_t first_cap = tda_vec_cap(&v);
    const size_t before = tda_arena_stats(&arena).used;

    for (int32_t i = 1; i < 1000; ++i) {
        TDA_TEST_OK(tda_vec_push(&v, &i));
    }

    TEST_ASSERT_EQUAL_size_t(1024, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_size_t((1024 - first_cap) * sizeof(int32_t), tda_arena_stats(&arena).used - before);

    tda_vec_deinit(&v);
    tda_arena_deinit(&arena);
}

static void test_extend_moves_wide_elems_whole() {
    tda_Vec v = TDA_VEC_INIT(Pair, tda_al_default());

    constexpr Pair src[2] = {{1, 10}, {2, 20}};
    TDA_TEST_OK(tda_vec_extend(&v, TDA_SPAN_FROM_DATA(Pair, src, 2)));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT64(10, TDA_VEC_GET_AS(Pair, &v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, &v, 1)->a);

    tda_vec_deinit(&v);
}

static void test_insert_span_puts_the_run_before_the_index() {
    tda_Vec v = make_vec(4); // 0, 1, 2, 3
    constexpr int32_t src[2] = {8, 9};

    TDA_TEST_OK(tda_vec_insert_span(&v, 1, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    constexpr int32_t want[6] = {0, 8, 9, 1, 2, 3};
    assert_elems(&v, want, 6);

    tda_vec_deinit(&v);
}

static void test_insert_span_at_the_front() {
    tda_Vec v = make_vec(3);
    constexpr int32_t src[2] = {8, 9};

    TDA_TEST_OK(tda_vec_insert_span(&v, 0, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    constexpr int32_t want[5] = {8, 9, 0, 1, 2};
    assert_elems(&v, want, 5);

    tda_vec_deinit(&v);
}

// idx == len does what extend does; extend is implemented as this call
static void test_insert_span_at_len_is_extend() {
    tda_Vec v = make_vec(3);
    constexpr int32_t src[2] = {8, 9};

    TDA_TEST_OK(tda_vec_insert_span(&v, tda_vec_len(&v), TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    constexpr int32_t want[5] = {0, 1, 2, 8, 9};
    assert_elems(&v, want, 5);

    tda_vec_deinit(&v);
}

static void test_insert_span_of_an_empty_span_changes_nothing() {
    tda_Vec v = make_vec(3);
    const void *before = tda_vec_data(&v);

    TDA_TEST_OK(tda_vec_insert_span(&v, 1, TDA_SPAN_FROM_DATA(int32_t, nullptr, 0)));

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(&v, want, 3);
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_vec_deinit(&v);
}

// the tail moves once, not once per elem, and a tail longer than the run catches a
// wrong shift
static void test_insert_span_keeps_a_long_tail_in_order() {
    tda_Vec v = make_vec(16);
    constexpr int32_t src[2] = {100, 200};

    TDA_TEST_OK(tda_vec_insert_span(&v, 2, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    TEST_ASSERT_EQUAL_size_t(18, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_VEC_GET_AS(int32_t, &v, 1));
    TEST_ASSERT_EQUAL_INT32(100, *TDA_VEC_GET_AS(int32_t, &v, 2));
    TEST_ASSERT_EQUAL_INT32(200, *TDA_VEC_GET_AS(int32_t, &v, 3));
    for (size_t i = 4; i < 18; ++i) {
        TEST_ASSERT_EQUAL_INT32((int32_t) i - 2, *TDA_VEC_GET_AS(int32_t, &v, i));
    }

    tda_vec_deinit(&v);
}

static void test_remove_range_closes_the_gap() {
    tda_Vec v = make_vec(6); // 0 .. 5

    tda_vec_remove_range(&v, 1, 3);

    constexpr int32_t want[3] = {0, 4, 5};
    assert_elems(&v, want, 3);

    tda_vec_deinit(&v);
}

static void test_remove_range_at_the_front() {
    tda_Vec v = make_vec(5);

    tda_vec_remove_range(&v, 0, 2);

    constexpr int32_t want[3] = {2, 3, 4};
    assert_elems(&v, want, 3);

    tda_vec_deinit(&v);
}

// nothing follows the gap, so there is no tail to move
static void test_remove_range_to_the_end() {
    tda_Vec v = make_vec(5);

    tda_vec_remove_range(&v, 3, 2);

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(&v, want, 3);

    tda_vec_deinit(&v);
}

static void test_remove_range_of_zero_changes_nothing() {
    tda_Vec v = make_vec(4);

    tda_vec_remove_range(&v, 2, 0);

    constexpr int32_t want[4] = {0, 1, 2, 3};
    assert_elems(&v, want, 4);

    tda_vec_deinit(&v);
}

static void test_remove_range_of_everything_empties_the_vec() {
    tda_Vec v = make_vec(4);

    tda_vec_remove_range(&v, 0, 4);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

// dropping elems frees nothing, as with pop and clear
static void test_remove_range_leaves_the_capacity_alone() {
    tda_Vec v = make_vec(6);
    const size_t cap = tda_vec_cap(&v);
    const void *before = tda_vec_data(&v);

    tda_vec_remove_range(&v, 1, 2);

    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_vec_deinit(&v);
}

static void test_remove_range_moves_wide_elems_whole() {
    tda_Vec v = TDA_VEC_INIT(Pair, tda_al_default());

    constexpr Pair src[4] = {{1, 10}, {2, 20}, {3, 30}, {4, 40}};
    TDA_TEST_OK(tda_vec_extend(&v, TDA_SPAN_FROM_DATA(Pair, src, 4)));

    tda_vec_remove_range(&v, 1, 2);

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, &v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, &v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(40, TDA_VEC_GET_AS(Pair, &v, 1)->b);

    tda_vec_deinit(&v);
}

// pushes each elem it sees onto the vec at 'ctx' and keeps them all
static bool record(const void *elem, void *ctx) {
    TDA_TEST_OK(tda_vec_push(ctx, elem));

    return true;
}

static void test_retain_keeps_what_passes_in_order() {
    tda_Vec v = make_vec(7); // 0 .. 6
    const size_t cap = tda_vec_cap(&v);

    tda_vec_retain(&v, tda_test_i32_is_even, nullptr);

    constexpr int32_t want[4] = {0, 2, 4, 6};
    assert_elems(&v, want, 4);
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(&v));

    tda_vec_deinit(&v);
}

// pred sees every elem once, front to back
static void test_retain_asks_once_per_elem_front_to_back() {
    tda_Vec v = make_vec(5);
    tda_Vec seen = make_vec_cap(5);

    tda_vec_retain(&v, record, &seen);

    constexpr int32_t want[5] = {0, 1, 2, 3, 4};
    assert_elems(&seen, want, 5);
    assert_elems(&v, want, 5);

    tda_vec_deinit(&seen);
    tda_vec_deinit(&v);
}

static void test_retain_of_none_empties_the_vec() {
    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_al_default(), &v, 1, 3, 5));

    tda_vec_retain(&v, tda_test_i32_is_even, nullptr);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

static void test_retain_on_an_empty_vec_asks_nothing() {
    tda_Vec v = make_vec_cap(0);
    tda_Vec seen = make_vec_cap(1);

    tda_vec_retain(&v, record, &seen);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&seen));

    tda_vec_deinit(&seen);
    tda_vec_deinit(&v);
}

static void test_retain_moves_whole_elems() {
    constexpr Pair src[4] = {{1, 10}, {-2, 20}, {3, 30}, {-4, 40}};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 4, tda_al_default(), &v));

    tda_vec_retain(&v, tda_test_pair_a_is_positive, nullptr);

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, &v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(30, TDA_VEC_GET_AS(Pair, &v, 1)->b);

    tda_vec_deinit(&v);
}

/* ========== clear ========== */

static void test_clear_drops_the_len_and_keeps_the_buffer() {
    tda_Vec v = make_vec(4);
    const void *before = tda_vec_data(&v);

    tda_vec_clear(&v);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    // the room is still there, so pushing back needs no allocation
    push_int(&v, 7);
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));
    TEST_ASSERT_EQUAL_INT32(7, *TDA_VEC_GET_AS(int32_t, &v, 0));

    tda_vec_deinit(&v);
}

static void test_clear_of_an_empty_vec_is_a_noop() {
    tda_Vec v = make_vec(0);

    tda_vec_clear(&v);

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));
    TEST_ASSERT_NULL(tda_vec_data(&v));

    tda_vec_deinit(&v);
}

/* ========== reserve ========== */

static void test_reserve_grows_the_capacity_and_keeps_the_contents() {
    tda_Vec v = make_vec(3);

    TDA_TEST_OK(tda_vec_reserve(&v, 16));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(16, tda_vec_cap(&v));

    constexpr int32_t want[3] = {0, 1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 3);

    tda_vec_deinit(&v);
}

// reserve never shrinks: a request at or below the current capacity changes nothing
static void test_reserve_below_the_capacity_is_a_noop() {
    tda_Vec v = make_vec_cap(8);
    const void *before = tda_vec_data(&v);

    TDA_TEST_OK(tda_vec_reserve(&v, 2));
    TDA_TEST_OK(tda_vec_reserve(&v, 8));
    TDA_TEST_OK(tda_vec_reserve(&v, 0));

    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_vec_deinit(&v);
}

// new_cap * elem_size overflows size_t: reported, never attempted
static void test_reserve_reports_size_overflow() {
    tda_Vec v = make_vec(3);
    const void *before = tda_vec_data(&v);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_reserve(&v, SIZE_MAX));

    // the vec is left as it was
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_vec_deinit(&v);
}

/* ========== shrink_to_fit ========== */

static void test_shrink_to_fit_drops_the_slack() {
    tda_Vec v = make_vec_cap(16);
    push_int(&v, 1);
    push_int(&v, 2);
    push_int(&v, 3);

    TDA_TEST_OK(tda_vec_shrink_to_fit(&v));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(&v));

    constexpr int32_t want[3] = {1, 2, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 3);

    tda_vec_deinit(&v);
}

static void test_shrink_to_fit_when_already_exact_is_a_noop() {
    tda_Vec v = make_vec(4);
    const void *before = tda_vec_data(&v);

    TDA_TEST_OK(tda_vec_shrink_to_fit(&v));

    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_vec_deinit(&v);
}

// shrinking an empty vec releases its buffer instead of resizing it to zero
static void test_shrink_to_fit_of_empty_releases_the_buffer() {
    tda_Vec v = make_vec_cap(8);

    TDA_TEST_OK(tda_vec_shrink_to_fit(&v));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(&v));
    TEST_ASSERT_NULL(tda_vec_data(&v));

    // and the vec stays usable afterwards
    push_int(&v, 5);
    TEST_ASSERT_EQUAL_INT32(5, *TDA_VEC_GET_AS(int32_t, &v, 0));

    tda_vec_deinit(&v);
}

static void test_shrink_to_fit_of_an_unallocated_vec_is_a_noop() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    TDA_TEST_OK(tda_vec_shrink_to_fit(&v));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_cap(&v));
    TEST_ASSERT_NULL(tda_vec_data(&v));

    tda_vec_deinit(&v);
}

/* ========== resize ========== */

static void test_resize_up_zeroes_the_tail() {
    tda_Vec v = make_vec(2); // 0, 1

    TDA_TEST_OK(tda_vec_resize(&v, 5));

    constexpr int32_t want[5] = {0, 1, 0, 0, 0};
    TEST_ASSERT_EQUAL_size_t(5, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 5);

    tda_vec_deinit(&v);
}

static void test_resize_down_keeps_the_head_and_the_capacity() {
    tda_Vec v = make_vec(5);

    TDA_TEST_OK(tda_vec_resize(&v, 2));

    constexpr int32_t want[2] = {0, 1};
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(5, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 2);

    tda_vec_deinit(&v);
}

// growing within the capacity zeroes the new tail without moving the buffer
static void test_resize_within_the_capacity_does_not_reallocate() {
    tda_Vec v = make_vec_cap(8);
    push_int(&v, 1);
    push_int(&v, 2);
    const void *before = tda_vec_data(&v);

    TDA_TEST_OK(tda_vec_resize(&v, 5));

    constexpr int32_t want[5] = {1, 2, 0, 0, 0};
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));
    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 5);

    tda_vec_deinit(&v);
}

static void test_resize_to_zero_keeps_the_buffer() {
    tda_Vec v = make_vec(4);
    const void *before = tda_vec_data(&v);

    TDA_TEST_OK(tda_vec_resize(&v, 0));

    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(4, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_vec_deinit(&v);
}

static void test_resize_to_the_same_len_is_a_noop() {
    tda_Vec v = make_vec(3);

    TDA_TEST_OK(tda_vec_resize(&v, 3));

    constexpr int32_t want[3] = {0, 1, 2};
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 3);

    tda_vec_deinit(&v);
}

static void test_resize_reports_size_overflow() {
    tda_Vec v = make_vec(3);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_resize(&v, SIZE_MAX));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(&v));

    tda_vec_deinit(&v);
}

static void test_truncate_drops_the_tail_and_keeps_the_block() {
    tda_Vec v = make_vec(5);
    const size_t cap = tda_vec_cap(&v);
    const void *before = tda_vec_data(&v);

    tda_vec_truncate(&v, 2);

    constexpr int32_t want[2] = {0, 1};
    assert_elems(&v, want, 2);
    TEST_ASSERT_EQUAL_size_t(cap, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_vec_truncate(&v, 0);
    TEST_ASSERT_EQUAL_size_t(0, tda_vec_len(&v));

    tda_vec_deinit(&v);
}

// unlike resize, a len past the end is not a request to grow
static void test_truncate_at_or_above_the_len_changes_nothing() {
    tda_Vec v = make_vec(3);

    tda_vec_truncate(&v, 3);
    tda_vec_truncate(&v, 10);

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(&v, want, 3);
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(&v));

    tda_vec_deinit(&v);
}

/* ========== swap ========== */

static void test_swap_of_itself_changes_nothing() {
    tda_Vec v = make_vec(3);
    const void *before = tda_vec_data(&v);

    tda_vec_swap(&v, &v);

    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_VEC_GET_AS(int32_t, &v, 2));

    tda_vec_deinit(&v);
}

// one allocator on both sides: the buffers are handed over, never copied
static void test_swap_same_allocator_hands_over_buffers() {
    tda_Vec a = make_vec(2), b = make_vec(5);

    const void *pa = tda_vec_data(&a), *pb = tda_vec_data(&b);

    tda_vec_swap(&a, &b);

    TEST_ASSERT_EQUAL_PTR(pb, tda_vec_data(&a));
    TEST_ASSERT_EQUAL_PTR(pa, tda_vec_data(&b));
    TEST_ASSERT_EQUAL_size_t(5, tda_vec_len(&a));
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&b));

    tda_vec_deinit(&b);
    tda_vec_deinit(&a);
}

// nothing is reallocated, so spare capacity moves with its buffer
static void test_swap_same_allocator_carries_the_capacity() {
    tda_Vec a = make_vec_cap(16);
    push_int(&a, 1);

    tda_Vec b = make_vec(3);

    tda_vec_swap(&a, &b);

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&a));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(&a));
    TEST_ASSERT_EQUAL_size_t(1, tda_vec_len(&b));
    TEST_ASSERT_EQUAL_size_t(16, tda_vec_cap(&b));

    tda_vec_deinit(&b);
    tda_vec_deinit(&a);
}

// each block goes with the allocator it came from, and each is given back to it
static void test_swap_across_allocators_carries_each_allocator() {
    tda_TestProbe pa, pb;
    tda_test_probe_reset(&pa);
    tda_test_probe_reset(&pb);
    tda_Al al_a = tda_test_probe_full(&pa), al_b = tda_test_probe_full(&pb);

    tda_Vec a = {}, b = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, al_a, &a, 1, 2));
    TDA_TEST_OK(TDA_VEC_OF(int32_t, al_b, &b, 3, 4, 5));

    tda_vec_swap(&a, &b);

    TEST_ASSERT_TRUE(tda_al_eq(al_b, tda_vec_al(&a)));
    TEST_ASSERT_TRUE(tda_al_eq(al_a, tda_vec_al(&b)));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&a));

    tda_vec_deinit(&a);
    TEST_ASSERT_EQUAL_size_t(0, pb.live);
    tda_vec_deinit(&b);
    TEST_ASSERT_EQUAL_size_t(0, pa.live);
}

/* ========== swap_elems ========== */

static void test_swap_elems_exchanges_the_pair() {
    tda_Vec v = make_vec(4);

    tda_vec_swap_elems(&v, 0, 3);

    constexpr int32_t want[4] = {3, 1, 2, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 4);

    tda_vec_deinit(&v);
}

static void test_swap_elems_same_index_is_noop() {
    tda_Vec v = make_vec(3);

    tda_vec_swap_elems(&v, 1, 1);

    TEST_ASSERT_EQUAL_INT32(1, *TDA_VEC_GET_AS(int32_t, &v, 1));

    tda_vec_deinit(&v);
}

// elem_size drives the swap, so a type wider than a word must move whole
static void test_swap_elems_moves_wide_elems_whole() {
    constexpr Pair src[2] = {{1, 2}, {3, 4}};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, src, 2, tda_al_default(), &v));

    tda_vec_swap_elems(&v, 0, 1);

    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_VEC_GET_AS(Pair, &v, 0)->b);
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, &v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_VEC_GET_AS(Pair, &v, 1)->b);

    tda_vec_deinit(&v);
}

/* ========== to span ========== */

// the view covers the content, not the allocation
static void test_to_span_follows_the_len_not_the_capacity() {
    tda_Vec v = make_vec_cap(8);
    push_int(&v, 1);
    push_int(&v, 2);
    push_int(&v, 3);

    const tda_Span s = tda_vec_to_span(&v);

    TEST_ASSERT_EQUAL_PTR(tda_vec_data(&v), s.data);
    TEST_ASSERT_EQUAL_size_t(3, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem.size);
    TEST_ASSERT_EQUAL_INT32(3, *TDA_SPAN_GET_AS(int32_t, s, 2));

    tda_vec_deinit(&v);
}

static void test_to_span_mut_writes_reach_the_vec() {
    tda_Vec v = make_vec(4);

    const tda_SpanMut s = tda_vec_to_span_mut(&v);
    TDA_SPAN_SET(int32_t, s, 0, 77);

    TEST_ASSERT_EQUAL_INT32(77, *TDA_VEC_GET_AS(int32_t, &v, 0));

    tda_vec_deinit(&v);
}

static void test_to_span_of_empty_keeps_the_elem_layout() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    const tda_Span s = tda_vec_to_span(&v);

    TEST_ASSERT_NULL(s.data);
    TEST_ASSERT_EQUAL_size_t(0, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem.size);

    tda_vec_deinit(&v);
}

/* ========== allocation failure ========== */

// a 2-byte elem: SIZE_MAX of them is too many for any address space
static constexpr tda_Layout TWO_BYTES = {.size = 2, .align = 2};

// len * elem.size overflows size_t: reported, never attempted
static void test_init_len_reports_size_overflow() {
    tda_Vec v = {};

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_init_len(SIZE_MAX, TWO_BYTES, tda_al_default(), &v));

    TEST_ASSERT_NULL(v.al_.vt); // out is untouched on failure
}

static void test_init_cap_reports_size_overflow() {
    tda_Vec v = {};

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_init_cap(SIZE_MAX, TWO_BYTES, tda_al_default(), &v));

    TEST_ASSERT_NULL(v.al_.vt);
}

static void test_from_data_reports_size_overflow() {
    constexpr int32_t src[1] = {1};
    tda_Vec v = {};

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_from_data(src, SIZE_MAX, TWO_BYTES, tda_al_default(), &v));

    TEST_ASSERT_NULL(v.al_.vt);
}

static void test_init_len_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(64, tda_al_default(), &arena));

    tda_Vec v;

    TDA_TEST_POISON(v);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_VEC_INIT_LEN(int32_t, 1000, tda_arena_to_al(&arena), &v));

    TDA_TEST_ASSERT_POISONED(v);

    tda_arena_deinit(&arena);
}

// a push that cannot grow reports it and leaves the vec unchanged
static void test_push_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_CAP(int32_t, 2, tda_arena_to_al(&arena), &v));

    push_int(&v, 1);
    push_int(&v, 2);
    const void *before = tda_vec_data(&v);

    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_VEC_PUSH(int32_t, &v, 3));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    constexpr int32_t want[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 2);

    tda_arena_deinit(&arena);
}

// when doubling does not fit, growth falls back to a single extra slot
static void test_push_falls_back_to_one_more_slot_when_doubling_fails() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_CAP(Pair, 2, tda_arena_to_al(&arena), &v));

    TDA_TEST_OK(tda_vec_push(&v, &(Pair){1, 2}));
    TDA_TEST_OK(tda_vec_push(&v, &(Pair){3, 4}));

    // room for three elems and whatever padding puts them on their alignment, never the
    // four doubling would ask for
    static_assert(3 * sizeof(Pair) + alignof(Pair) - 1 < 4 * sizeof(Pair));
    tda_test_arena_leave(&arena, 3 * sizeof(Pair) + alignof(Pair) - 1);

    TDA_TEST_OK(tda_vec_push(&v, &(Pair){5, 6}));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(3, tda_vec_cap(&v));

    // the elems moved with the buffer
    TEST_ASSERT_EQUAL_INT64(1, TDA_VEC_GET_AS(Pair, &v, 0)->a);
    TEST_ASSERT_EQUAL_INT64(3, TDA_VEC_GET_AS(Pair, &v, 1)->a);
    TEST_ASSERT_EQUAL_INT64(5, TDA_VEC_GET_AS(Pair, &v, 2)->a);
    TEST_ASSERT_EQUAL_INT64(6, TDA_VEC_GET_AS(Pair, &v, 2)->b);

    tda_arena_deinit(&arena);
}

// a refused run leaves the vec's length, block and elems unchanged, with nothing of
// 'src' written
static void test_extend_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_CAP(int32_t, 2, tda_arena_to_al(&arena), &v));
    push_int(&v, 1);
    push_int(&v, 2);
    const void *before = tda_vec_data(&v);

    tda_test_arena_leave(&arena, 0);

    constexpr int32_t src[3] = {7, 8, 9};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_extend(&v, TDA_SPAN_FROM_DATA(int32_t, src, 3)));

    constexpr int32_t want[2] = {1, 2};
    assert_elems(&v, want, 2);
    TEST_ASSERT_EQUAL_size_t(2, tda_vec_cap(&v));
    TEST_ASSERT_EQUAL_PTR(before, tda_vec_data(&v));

    tda_arena_deinit(&arena);
}

// the same for a run into the middle: a failing call must not shift the tail
static void test_insert_span_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_CAP(int32_t, 3, tda_arena_to_al(&arena), &v));
    push_int(&v, 1);
    push_int(&v, 2);
    push_int(&v, 3);

    tda_test_arena_leave(&arena, 0);

    constexpr int32_t src[2] = {7, 8};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_insert_span(&v, 1, TDA_SPAN_FROM_DATA(int32_t, src, 2)));

    constexpr int32_t want[3] = {1, 2, 3};
    assert_elems(&v, want, 3);

    tda_arena_deinit(&arena);
}

// len + src.len overflows size_t: reported, and no byte is read from 'src'
static void test_extend_reports_size_overflow() {
    tda_Vec v = make_vec(3);

    constexpr int32_t src[1] = {1};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_extend(&v, TDA_SPAN_FROM_DATA(int32_t, src, SIZE_MAX)));

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(&v, want, 3);

    tda_vec_deinit(&v);
}

// a run that fits in the existing room never allocates, so an exhausted arena is fine
static void test_extend_within_the_capacity_needs_no_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_CAP(int32_t, 8, tda_arena_to_al(&arena), &v));
    push_int(&v, 1);

    tda_test_arena_leave(&arena, 0);

    constexpr int32_t src[3] = {7, 8, 9};
    TDA_TEST_OK(tda_vec_extend(&v, TDA_SPAN_FROM_DATA(int32_t, src, 3)));

    constexpr int32_t want[4] = {1, 7, 8, 9};
    assert_elems(&v, want, 4);

    tda_arena_deinit(&arena);
}

/* ========== macros ========== */

static void test_macro_of_builds_from_literals() {
    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_al_default(), &v, 4, 5, 6));

    TEST_ASSERT_EQUAL_size_t(3, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_layout(&v).size);

    constexpr int32_t want[3] = {4, 5, 6};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 3);

    tda_vec_deinit(&v);
}

static void test_macro_of_derives_len_from_the_list() {
    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_OF(int64_t, tda_al_default(), &v, 1, 2, 3, 4, 5));

    TEST_ASSERT_EQUAL_size_t(5, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int64_t), tda_vec_elem_layout(&v).size);

    tda_vec_deinit(&v);
}

static void test_macro_push_appends_a_value() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    TDA_TEST_OK(TDA_VEC_PUSH(int32_t, &v, 4));
    TDA_TEST_OK(TDA_VEC_PUSH(int32_t, &v, 5));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    constexpr int32_t want[2] = {4, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 2);

    tda_vec_deinit(&v);
}

// the value goes into a compound literal, so it is evaluated exactly once
static void test_macro_push_evaluates_its_value_once() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    int32_t next = 0;
    for (int i = 0; i < 3; ++i) {
        TDA_TEST_OK(TDA_VEC_PUSH(int32_t, &v, next++));
    }

    TEST_ASSERT_EQUAL_INT32(3, next);
    constexpr int32_t want[3] = {0, 1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, tda_vec_data(&v), 3);

    tda_vec_deinit(&v);
}

static void test_macro_from_data_takes_the_layout_from_the_type() {
    constexpr int32_t src[2] = {1, 2};

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(int32_t, src, 2, tda_al_default(), &v));

    TEST_ASSERT_EQUAL_size_t(2, tda_vec_len(&v));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_vec_elem_layout(&v).size);
    TEST_ASSERT_EQUAL_INT32_ARRAY(src, tda_vec_data(&v), 2);

    tda_vec_deinit(&v);
}

static void test_macro_extend_appends_a_literal_list() {
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());

    TDA_TEST_OK(TDA_VEC_EXTEND(int32_t, &v, 4, 5, 6));

    constexpr int32_t want[3] = {4, 5, 6};
    assert_elems(&v, want, 3);

    tda_vec_deinit(&v);
}

static void test_macro_insert_span_places_a_literal_list() {
    tda_Vec v = make_vec(2); // 0, 1

    TDA_TEST_OK(TDA_VEC_INSERT_SPAN(int32_t, &v, 1, 8, 9));

    constexpr int32_t want[4] = {0, 8, 9, 1};
    assert_elems(&v, want, 4);

    tda_vec_deinit(&v);
}

/* ========== compare ========== */

static void test_eq_matches_the_same_elems() {
    tda_Vec a = make_vec(4), b = make_vec(4);

    TEST_ASSERT_TRUE(tda_vec_eq(&a, &a));
    TEST_ASSERT_TRUE(tda_vec_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_vec_eq(&b, &a));
    TEST_ASSERT_TRUE(tda_vec_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_vec_deinit(&a);
    tda_vec_deinit(&b);
}

static void test_eq_parts_one_differing_elem() {
    tda_Vec a = make_vec(4), b = make_vec(4);
    TDA_VEC_SET(int32_t, &b, 3, 99);

    TEST_ASSERT_FALSE(tda_vec_eq(&a, &b));
    TEST_ASSERT_FALSE(tda_vec_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_vec_deinit(&a);
    tda_vec_deinit(&b);
}

static void test_eq_parts_different_lengths() {
    tda_Vec a = make_vec(4);
    tda_Vec shorter = make_vec(3);

    TEST_ASSERT_FALSE(tda_vec_eq(&a, &shorter));
    TEST_ASSERT_FALSE(tda_vec_eq(&shorter, &a));

    tda_vec_deinit(&a);
    tda_vec_deinit(&shorter);
}

static void test_eq_of_two_empties() {
    tda_Vec a = make_vec(0);
    tda_Vec b = make_vec_cap(8);
    tda_Vec one = make_vec(1);

    TEST_ASSERT_TRUE(tda_vec_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_vec_eq_by(&a, &b, tda_eq_i32, nullptr));
    TEST_ASSERT_FALSE(tda_vec_eq(&a, &one));

    tda_vec_deinit(&a);
    tda_vec_deinit(&b);
    tda_vec_deinit(&one);
}

// the two hold the same elems in blocks of different sizes
static void test_eq_ignores_capacity() {
    tda_Vec a = make_vec(3), b = make_vec_cap(64);
    for (int32_t i = 0; i < 3; ++i) {
        push_int(&b, i);
    }

    TEST_ASSERT_TRUE(tda_vec_cap(&a) != tda_vec_cap(&b));
    TEST_ASSERT_TRUE(tda_vec_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_vec_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_vec_deinit(&a);
    tda_vec_deinit(&b);
}

// a popped elem is gone even though its bytes are still in the block
static void test_eq_forgets_a_popped_elem() {
    tda_Vec a = make_vec(3), b = make_vec(4);

    TEST_ASSERT_FALSE(tda_vec_eq(&a, &b));
    tda_vec_pop(&b);
    TEST_ASSERT_TRUE(tda_vec_eq(&a, &b));

    tda_vec_deinit(&a);
    tda_vec_deinit(&b);
}

static void test_eq_by_asks_the_equality() {
    constexpr Pair lhs[2] = {{1, 10}, {2, 20}};
    constexpr Pair rhs[2] = {{1, 70}, {2, 80}};

    tda_Vec a = {}, b = {};
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, lhs, 2, tda_al_default(), &a));
    TDA_TEST_OK(TDA_VEC_FROM_DATA(Pair, rhs, 2, tda_al_default(), &b));

    TEST_ASSERT_FALSE(tda_vec_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_vec_eq_by(&a, &b, tda_test_pair_eq_a, nullptr));

    tda_vec_deinit(&a);
    tda_vec_deinit(&b);
}

/* ========== print ========== */

// printers write to a stream, so cases read it back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_Vec *v) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_vec_fprint(v, stream, tda_fprint_i32, nullptr);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_elems() {
    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_al_default(), &v, 5, 3, 1));

    assert_prints("[5, 3, 1]", &v);

    tda_vec_deinit(&v);
}

static void test_fprint_of_a_single_elem_has_no_separator() {
    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_OF(int32_t, tda_al_default(), &v, 7));

    assert_prints("[7]", &v);

    tda_vec_deinit(&v);
}

static void test_fprint_of_an_empty_vec() {
    tda_Vec v = make_vec(0);

    assert_prints("[]", &v);

    tda_vec_deinit(&v);
}

// stdout cannot be captured portably, so this only checks that the stdout variant runs
static void test_print_writes_to_stdout() {
    tda_Vec v = make_vec(3);

    tda_vec_print(&v, tda_fprint_i32, nullptr);

    tda_vec_deinit(&v);
}

// a value read from a packed buffer may sit at any address; insert copies it as push
// does, without asking it to be aligned
static void test_insert_takes_an_unaligned_val() {
    alignas(int32_t) unsigned char raw[8] = {};
    constexpr int32_t val = 42;
    memcpy(raw + 1, &val, sizeof(val));

    tda_Vec x = TDA_VEC_INIT(int32_t, tda_al_default());
    TDA_TEST_OK(tda_vec_insert(&x, 0, raw + 1));

    TEST_ASSERT_EQUAL_INT32(42, *TDA_VEC_GET_AS(int32_t, &x, 0));

    tda_vec_deinit(&x);
}

// a refused shrink keeps the block, and the elems in it
static void test_shrink_to_fit_reports_a_refused_block() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_Vec v = {};
    TDA_TEST_OK(TDA_VEC_INIT_CAP(int32_t, 8, al, &v));
    push_int(&v, 1);
    push_int(&v, 2);

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_vec_shrink_to_fit(&v));

    TEST_ASSERT_EQUAL_size_t(8, tda_vec_cap(&v));
    constexpr int32_t want[2] = {1, 2};
    assert_elems(&v, want, 2);

    probe.fail_after = SIZE_MAX;
    tda_vec_deinit(&v);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}


/* ========== print errors ========== */

// the ctx reaches every call, every elem is still asked after a failure, and the first
// failure is what comes back; print adds only its newline
static void test_fprint_hands_on_the_ctx_and_the_first_failure() {
    tda_Vec v = make_vec(3);
    tda_TestPrint pr = {.fail_from = 2};
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    TEST_ASSERT_EQUAL_INT(-2, tda_vec_fprint(&v, stream, tda_test_fprint_i32_failing, &pr));
    TEST_ASSERT_EQUAL_size_t(3, pr.calls);
    fclose(stream);

    pr = (tda_TestPrint){.fail_from = 0};
    TEST_ASSERT_EQUAL_INT(0, tda_vec_print(&v, tda_test_fprint_i32_failing, &pr));
    pr = (tda_TestPrint){.fail_from = 2};
    TEST_ASSERT_EQUAL_INT(-2, tda_vec_print(&v, tda_test_fprint_i32_failing, &pr));

    FILE *ro = tda_test_unwritable_stream();
    TEST_ASSERT_LESS_THAN_INT(0, tda_vec_fprint(&v, ro, tda_fprint_i32, nullptr));
    fclose(ro);

    tda_vec_deinit(&v);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_empty_and_unallocated);
    RUN_TEST(test_init_len_sets_len_cap_and_zeroes);
    RUN_TEST(test_init_cap_reserves_without_length);
    RUN_TEST(test_init_cap_zero_has_no_buffer);
    RUN_TEST(test_from_data_copies_and_detaches_the_source);
    RUN_TEST(test_from_data_empty_has_no_buffer);
    RUN_TEST(test_from_data_copies_whole_elems);
    RUN_TEST(test_from_span_copies_the_view);
    RUN_TEST(test_deinit_of_an_inert_value_is_noop);
    RUN_TEST(test_deinit_twice_is_noop);
    RUN_TEST(test_init_allocates_nothing);
    RUN_TEST(test_a_vec_lives_inside_a_struct);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_fits_the_len_not_the_source_capacity);
    RUN_TEST(test_copy_of_empty_has_no_buffer);
    RUN_TEST(test_copy_inherits_the_source_allocator);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_hands_the_allocator_over);
    RUN_TEST(test_move_assign_needs_no_memory);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_assign_grows_and_shrinks_the_len);
    RUN_TEST(test_copy_assign_keeps_the_target_capacity);
    RUN_TEST(test_copy_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_assign_keeps_the_target_allocator);

    RUN_TEST(test_bytes_is_len_times_elem_size);
    RUN_TEST(test_bytes_ignores_the_spare_capacity);
    RUN_TEST(test_bytes_of_empty_is_zero);
    RUN_TEST(test_bytes_tracks_elem_size);
    RUN_TEST(test_bytes_agrees_with_the_span);

    RUN_TEST(test_set_get_roundtrip);
    RUN_TEST(test_set_from_its_own_elem);
    RUN_TEST(test_get_mut_writes_through);
    RUN_TEST(test_first_and_last_address_the_ends);
    RUN_TEST(test_first_and_last_mut_write_through);
    RUN_TEST(test_first_and_last_coincide_on_a_single_elem);
    RUN_TEST(test_last_follows_the_len);
    RUN_TEST(test_data_mut_writes_through);

    RUN_TEST(test_push_appends_and_doubles_the_capacity);
    RUN_TEST(test_push_uses_reserved_capacity_without_reallocating);
    RUN_TEST(test_push_assume_cap_after_a_reserve_allocates_nothing);
    RUN_TEST(test_push_assume_cap_takes_an_elem_of_its_own);
    RUN_TEST(test_add_one_hands_out_the_new_back);
    RUN_TEST(test_add_one_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_add_one_assume_cap_allocates_nothing);
    RUN_TEST(test_push_moves_whole_elems);
    RUN_TEST(test_pop_shortens_and_keeps_the_capacity);
    RUN_TEST(test_push_after_pop_overwrites_the_slot);
    RUN_TEST(test_pop_to_hands_the_back_elem_out);
    RUN_TEST(test_pop_to_into_the_vec_itself);
    RUN_TEST(test_pop_to_moves_whole_elems);

    RUN_TEST(test_insert_at_front_middle_and_end);
    RUN_TEST(test_insert_into_an_empty_vec);
    RUN_TEST(test_insert_grows_when_full);
    RUN_TEST(test_remove_from_front_middle_and_end);
    RUN_TEST(test_remove_of_the_last_elem_empties_the_vec);
    RUN_TEST(test_insert_and_remove_move_whole_elems);
    RUN_TEST(test_swap_remove_fills_the_gap_with_the_back);
    RUN_TEST(test_swap_remove_of_the_back_elem);
    RUN_TEST(test_swap_remove_moves_whole_elems);

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
    RUN_TEST(test_retain_keeps_what_passes_in_order);
    RUN_TEST(test_retain_asks_once_per_elem_front_to_back);
    RUN_TEST(test_retain_of_none_empties_the_vec);
    RUN_TEST(test_retain_on_an_empty_vec_asks_nothing);
    RUN_TEST(test_retain_moves_whole_elems);

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
    RUN_TEST(test_truncate_drops_the_tail_and_keeps_the_block);
    RUN_TEST(test_truncate_at_or_above_the_len_changes_nothing);

    RUN_TEST(test_swap_of_itself_changes_nothing);
    RUN_TEST(test_swap_same_allocator_hands_over_buffers);
    RUN_TEST(test_swap_same_allocator_carries_the_capacity);
    RUN_TEST(test_swap_across_allocators_carries_each_allocator);

    RUN_TEST(test_swap_elems_exchanges_the_pair);
    RUN_TEST(test_swap_elems_same_index_is_noop);
    RUN_TEST(test_swap_elems_moves_wide_elems_whole);

    RUN_TEST(test_to_span_follows_the_len_not_the_capacity);
    RUN_TEST(test_to_span_mut_writes_reach_the_vec);
    RUN_TEST(test_to_span_of_empty_keeps_the_elem_layout);

    RUN_TEST(test_init_len_reports_size_overflow);
    RUN_TEST(test_init_cap_reports_size_overflow);
    RUN_TEST(test_from_data_reports_size_overflow);
    RUN_TEST(test_init_len_reports_an_exhausted_arena);
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
    RUN_TEST(test_macro_from_data_takes_the_layout_from_the_type);
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

    RUN_TEST(test_insert_takes_an_unaligned_val);

    RUN_TEST(test_shrink_to_fit_reports_a_refused_block);

    RUN_TEST(test_fprint_hands_on_the_ctx_and_the_first_failure);

    return UNITY_END();
}
