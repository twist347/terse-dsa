#include "tda/ds/list.h"
#include "tda/algo/search.h"
#include "tda/algo/sort.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/alloc/pool.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/util.h"

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

static void push_int(tda_List *l, int32_t val) {
    TDA_TEST_OK(tda_list_push_back(l, &val));
}

// int32_t list holding 0, 1, ... len-1
static tda_List make_list(size_t len) {
    tda_List l = TDA_LIST_INIT(int32_t, tda_al_default());

    for (size_t i = 0; i < len; ++i) {
        push_int(&l, (int32_t) i);
    }

    return l;
}

// checks the contents in BOTH directions: a broken next/prev pair leaves one of the
// two walks intact, so a one-way check misses half the bugs
static void assert_elems(const tda_List *l, const int32_t *want, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, tda_list_len(l));

    size_t seen = 0;
    for (const tda_ListNode *node = tda_list_front_node(l); node; node = tda_list_node_next(node)) {
        TEST_ASSERT_TRUE_MESSAGE(seen < n, "forward walk is longer than expected");
        TEST_ASSERT_EQUAL_INT32(want[seen], *TDA_LIST_NODE_ELEM_AS(int32_t, l, node));
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(n, seen);

    seen = 0;
    for (const tda_ListNode *node = tda_list_back_node(l); node; node = tda_list_node_prev(node)) {
        TEST_ASSERT_TRUE_MESSAGE(seen < n, "backward walk is longer than expected");
        TEST_ASSERT_EQUAL_INT32(want[n - seen - 1], *TDA_LIST_NODE_ELEM_AS(int32_t, l, node));
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(n, seen);

    if (n > 0) {
        TEST_ASSERT_EQUAL_INT32(want[0], *TDA_LIST_FRONT_AS(int32_t, l));
        TEST_ASSERT_EQUAL_INT32(want[n - 1], *TDA_LIST_BACK_AS(int32_t, l));
    } else {
        TEST_ASSERT_NULL(tda_list_front_node(l));
        TEST_ASSERT_NULL(tda_list_back_node(l));
    }
}

[[nodiscard]]
static tda_ListNode *node_at(tda_List *l, size_t idx) {
    tda_ListNode *node = tda_list_front_node_mut(l);
    for (size_t i = 0; i < idx; ++i) {
        node = tda_list_node_next_mut(node);
    }
    TEST_ASSERT_NOT_NULL(node);

    return node;
}

// the node addresses, front to back: a relinking op preserves them, a copying one cannot
static void collect_nodes(const tda_List *l, const tda_ListNode **dst, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, tda_list_len(l));

    size_t i = 0;
    for (const tda_ListNode *node = tda_list_front_node(l); node; node = tda_list_node_next(node)) {
        dst[i++] = node;
    }
}

// every node of 'want' is still a node of 'l', in whatever order
static void assert_same_nodes(const tda_List *l, const tda_ListNode **want, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, tda_list_len(l));

    for (size_t i = 0; i < n; ++i) {
        bool found = false;
        for (const tda_ListNode *node = tda_list_front_node(l); node; node = tda_list_node_next(node)) {
            if (node == want[i]) {
                found = true;
                break;
            }
        }
        TEST_ASSERT_TRUE_MESSAGE(found, "a node was replaced instead of relinked");
    }
}

static void assert_empty(const tda_List *l) {
    assert_elems(l, nullptr, 0);
}

/* ========== lifetime ========== */

static void test_init_starts_empty() {
    tda_List l = TDA_LIST_INIT(int32_t, tda_al_default());

    assert_empty(&l);

    tda_list_deinit(&l);
}

// a pool built on node_layout carries every node, small elem or large,
// loosely aligned or not, and gets each block back on the deinit
static void test_node_layout_fits_a_pool_to_the_list() {
    constexpr tda_Layout elems[] = {
        {.size = 1, .align = 1},
        {.size = 4, .align = 4},
        {.size = 24, .align = 8},
        {.size = 64, .align = 64},
        {.size = 200, .align = 8},
    };
    constexpr size_t count = 8;

    for (size_t k = 0; k < sizeof(elems) / sizeof(elems[0]); ++k) {
        tda_Pool pool;
        TDA_TEST_OK(tda_pool_init_cap(count, tda_list_node_layout(elems[k]), tda_al_default(), &pool));

        tda_List l = tda_list_init(elems[k], tda_pool_to_al(&pool));

        constexpr unsigned char val[200] = {};
        for (size_t i = 0; i < count; ++i) {
            TDA_TEST_OK(tda_list_push_back(&l, val));
        }
        TEST_ASSERT_EQUAL_size_t(0, tda_pool_stats(&pool).free);

        tda_list_deinit(&l);
        TEST_ASSERT_EQUAL_size_t(count, tda_pool_stats(&pool).free);
        tda_pool_deinit(&pool);
    }
}

static tda_Layout bytes(size_t size) {
    return (tda_Layout){.size = size, .align = 1};
}

static void test_node_layout_grows_with_the_elem() {
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(64, tda_list_node_layout(bytes(64)).size);
    TEST_ASSERT_EQUAL_size_t(tda_list_node_layout(bytes(64)).size + 1000, tda_list_node_layout(bytes(1064)).size);
    TEST_ASSERT_EQUAL_size_t(tda_list_node_layout(bytes(1)).size, tda_list_node_layout(bytes(2)).size);

    // the links set the floor of the alignment, and an elem above it raises it
    TEST_ASSERT_EQUAL_size_t(alignof(void *), tda_list_node_layout(bytes(1)).align);
    TEST_ASSERT_EQUAL_size_t(64, tda_list_node_layout((tda_Layout){.size = 64, .align = 64}).align);
}

static void test_init_keeps_the_elem_layout_and_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    tda_List l = TDA_LIST_INIT(Pair, tda_arena_to_al(&arena));

    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_list_elem_layout(&l).size);
    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_list_al(&l)));

    tda_list_deinit(&l);
    tda_arena_deinit(&arena);
}

static void test_from_data_copies_and_detaches_the_source() {
    int32_t src[] = {1, 2, 3};
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_FROM_DATA(int32_t, src, 3, tda_al_default(), &l));

    src[0] = 99; // the list must not follow the source

    assert_elems(&l, (int32_t[]){1, 2, 3}, 3);

    tda_list_deinit(&l);
}

static void test_from_data_empty_is_empty() {
    tda_List l = {};
    TDA_TEST_OK(tda_list_from_data(nullptr, 0, TDA_LAYOUT_OF(int32_t), tda_al_default(), &l));

    assert_empty(&l);

    tda_list_deinit(&l);
}

// a wide elem must travel whole, not truncated to a word
static void test_from_data_copies_whole_elems() {
    constexpr Pair src[] = {{1, 2}, {3, 4}};
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_FROM_DATA(Pair, src, 2, tda_al_default(), &l));

    const Pair *first = TDA_LIST_FRONT_AS(Pair, &l), *last = TDA_LIST_BACK_AS(Pair, &l);

    TEST_ASSERT_EQUAL_INT64(1, first->a);
    TEST_ASSERT_EQUAL_INT64(2, first->b);
    TEST_ASSERT_EQUAL_INT64(3, last->a);
    TEST_ASSERT_EQUAL_INT64(4, last->b);

    tda_list_deinit(&l);
}

static void test_from_span_copies_the_view() {
    constexpr int32_t src[] = {5, 6, 7};
    tda_List l = {};
    TDA_TEST_OK(tda_list_from_span(TDA_SPAN_FROM_DATA(int32_t, src, 3), tda_al_default(), &l));

    assert_elems(&l, (int32_t[]){5, 6, 7}, 3);

    tda_list_deinit(&l);
}

static void test_deinit_of_an_inert_value_is_noop() {
    tda_List x = {};
    tda_list_deinit(&x);
    tda_list_deinit(&x);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_List l = make_list(3);
    tda_List c = {};
    TDA_TEST_OK(tda_list_copy(&l, &c));

    tda_list_pop_back(&c);
    push_int(&l, 9);

    assert_elems(&l, (int32_t[]){0, 1, 2, 9}, 4);
    assert_elems(&c, (int32_t[]){0, 1}, 2);

    tda_list_deinit(&c);
    tda_list_deinit(&l);
}

static void test_copy_inherits_the_source_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &l, 1, 2));

    tda_List c = {};
    TDA_TEST_OK(tda_list_copy(&l, &c));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_list_al(&c)));

    tda_arena_deinit(&arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_List src = make_list(4);

    tda_List dst = {};
    TDA_TEST_OK(tda_list_copy_with(&src, tda_arena_to_al(&arena), &dst));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_list_al(&dst)));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_list_al(&src)));
    TEST_ASSERT_TRUE(tda_list_eq(&src, &dst));

    // the copy outlives the source, so every node is its own
    tda_list_deinit(&src);
    assert_elems(&dst, (int32_t[]){0, 1, 2, 3}, 4);

    tda_list_deinit(&dst);
    tda_arena_deinit(&arena);
}

// the nodes come from the copy's allocator, not the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));
    tda_test_arena_leave(&arena, 0);

    tda_List src = make_list(4);

    tda_List dst;
    TDA_TEST_POISON(dst);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_list_copy_with(&src, tda_arena_to_al(&arena), &dst));
    TDA_TEST_ASSERT_POISONED(dst);
    TEST_ASSERT_EQUAL_size_t(4, tda_list_len(&src));

    tda_list_deinit(&src);
    tda_arena_deinit(&arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_List src = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &src, 1, 2, 3));

    tda_List dst = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &dst, 9));

    // on one allocator the nodes are relinked in place, so a position taken from the
    // source before the move stays valid and then belongs to 'dst'
    const tda_ListNode *node = tda_list_front_node(&src);

    const size_t requests = tda_test_probe_requests(&probe);
    tda_list_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    assert_elems(&dst, (int32_t[]){1, 2, 3}, 3);
    TEST_ASSERT_EQUAL_PTR(node, tda_list_front_node(&dst));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_LIST_NODE_ELEM_AS(int32_t, &dst, node));

    TEST_ASSERT_EQUAL_size_t(0, tda_list_len(&src));
    TEST_ASSERT_NULL(tda_list_front_node(&src));

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the allocator is part of the value: 'dst' takes the elems with the allocator they came
// from
static void test_move_assign_across_allocators_hands_the_allocator_over() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_List src = make_list(4);

    tda_List dst = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &dst, 9));

    tda_list_move_assign(&dst, &src);

    assert_elems(&dst, (int32_t[]){0, 1, 2, 3}, 4);
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_list_al(&dst)));

    TEST_ASSERT_EQUAL_size_t(0, tda_list_len(&src));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_list_al(&src)));

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
    tda_arena_deinit(&arena);
}

// nothing is allocated, so an exhausted allocator on either side cannot stop it
static void test_move_assign_needs_no_memory() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_List dst = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &dst, 9));
    tda_test_arena_leave(&arena, 0);

    tda_List src = make_list(4);

    tda_list_move_assign(&dst, &src);

    assert_elems(&dst, (int32_t[]){0, 1, 2, 3}, 4);
    assert_elems(&src, nullptr, 0);

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
    tda_arena_deinit(&arena);
}

// an inert target, `= {}`, takes on everything the source has: layout, allocator and
// all, and the source stays usable
static void test_move_assign_fills_an_inert_target() {
    tda_List src = make_list(4), dst = {};

    tda_list_move_assign(&dst, &src);

    assert_elems(&dst, (int32_t[]){0, 1, 2, 3}, 4);
    push_int(&src, 7);
    assert_elems(&src, (int32_t[]){7}, 1);

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_List l = make_list(3);

    tda_list_move_assign(&l, &l);

    assert_elems(&l, (int32_t[]){0, 1, 2}, 3);

    tda_list_deinit(&l);
}

static void test_copy_of_empty_is_empty() {
    tda_List l = make_list(0);
    tda_List c = {};
    TDA_TEST_OK(tda_list_copy(&l, &c));

    assert_empty(&c);

    tda_list_deinit(&c);
    tda_list_deinit(&l);
}

static void test_copy_assign_grows_the_target() {
    tda_List src = make_list(4), dst = make_list(1);

    TDA_TEST_OK(tda_list_copy_assign(&dst, &src));

    assert_elems(&dst, (int32_t[]){0, 1, 2, 3}, 4);
    assert_elems(&src, (int32_t[]){0, 1, 2, 3}, 4);

    tda_list_deinit(&dst);
    tda_list_deinit(&src);
}

static void test_copy_assign_shrinks_the_target() {
    tda_List src = make_list(1), dst = make_list(4);

    TDA_TEST_OK(tda_list_copy_assign(&dst, &src));

    assert_elems(&dst, (int32_t[]){0}, 1);

    tda_list_deinit(&dst);
    tda_list_deinit(&src);
}

static void test_copy_assign_from_empty_empties_the_target() {
    tda_List src = make_list(0), dst = make_list(3);

    TDA_TEST_OK(tda_list_copy_assign(&dst, &src));

    assert_empty(&dst);
    push_int(&dst, 7); // still usable
    assert_elems(&dst, (int32_t[]){7}, 1);

    tda_list_deinit(&dst);
    tda_list_deinit(&src);
}

static void test_copy_assign_to_empty_target() {
    tda_List src = make_list(2), dst = make_list(0);

    TDA_TEST_OK(tda_list_copy_assign(&dst, &src));

    assert_elems(&dst, (int32_t[]){0, 1}, 2);

    tda_list_deinit(&dst);
    tda_list_deinit(&src);
}

static void test_copy_assign_of_itself_changes_nothing() {
    tda_List l = make_list(3);

    TDA_TEST_OK(tda_list_copy_assign(&l, &l));

    assert_elems(&l, (int32_t[]){0, 1, 2}, 3);

    tda_list_deinit(&l);
}

static void test_copy_assign_keeps_the_target_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_List src = make_list(2);
    tda_List dst = TDA_LIST_INIT(int32_t, tda_arena_to_al(&arena));

    TDA_TEST_OK(tda_list_copy_assign(&dst, &src));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_list_al(&dst)));
    assert_elems(&dst, (int32_t[]){0, 1}, 2);

    tda_list_deinit(&src);
    tda_arena_deinit(&arena);
}

// the shared prefix keeps its nodes, so assignment allocates only for elems the target
// had no node for
static void test_copy_assign_reuses_the_target_nodes() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List src = make_list(2), dst = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &dst, 7, 8));

    const tda_ListNode *head = tda_list_front_node(&dst), *tail = tda_list_back_node(&dst);
    const size_t before = tda_test_probe_requests(&probe);

    TDA_TEST_OK(tda_list_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_PTR(head, tda_list_front_node(&dst));
    TEST_ASSERT_EQUAL_PTR(tail, tda_list_back_node(&dst));
    assert_elems(&dst, (int32_t[]){0, 1}, 2);

    tda_list_deinit(&dst);
    tda_list_deinit(&src);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== info ========== */

static void test_len_counts_the_elems() {
    tda_List l = make_list(0);
    TEST_ASSERT_EQUAL_size_t(0, tda_list_len(&l));

    push_int(&l, 1);
    TEST_ASSERT_EQUAL_size_t(1, tda_list_len(&l));

    push_int(&l, 2);
    TEST_ASSERT_EQUAL_size_t(2, tda_list_len(&l));

    tda_list_pop_front(&l);
    TEST_ASSERT_EQUAL_size_t(1, tda_list_len(&l));

    tda_list_deinit(&l);
}

static void test_elem_layout_is_the_init_arg() {
    tda_List l = TDA_LIST_INIT(Pair, tda_al_default());

    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_list_elem_layout(&l).size);

    tda_list_deinit(&l);
}

static void test_al_is_the_init_arg() {
    tda_List l = make_list(0);

    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_list_al(&l)));

    tda_list_deinit(&l);
}

/* ========== access ========== */

static void test_first_and_last_see_both_ends() {
    tda_List l = make_list(3);

    TEST_ASSERT_EQUAL_INT32(0, *TDA_LIST_FRONT_AS(int32_t, &l));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_LIST_BACK_AS(int32_t, &l));

    tda_list_deinit(&l);
}

static void test_first_and_last_meet_on_a_single_elem() {
    tda_List l = make_list(1);

    TEST_ASSERT_EQUAL_PTR(tda_list_front(&l), tda_list_back(&l));

    tda_list_deinit(&l);
}

static void test_first_mut_writes_through() {
    tda_List l = make_list(3);

    *TDA_LIST_FRONT_MUT_AS(int32_t, &l) = 9;

    assert_elems(&l, (int32_t[]){9, 1, 2}, 3);

    tda_list_deinit(&l);
}

static void test_last_mut_writes_through() {
    tda_List l = make_list(3);

    *TDA_LIST_BACK_MUT_AS(int32_t, &l) = 9;

    assert_elems(&l, (int32_t[]){0, 1, 9}, 3);

    tda_list_deinit(&l);
}

/* ========== nodes ========== */

static void test_nodes_of_an_empty_list_are_null() {
    tda_List l = make_list(0);

    TEST_ASSERT_NULL(tda_list_front_node(&l));
    TEST_ASSERT_NULL(tda_list_back_node(&l));

    tda_list_deinit(&l);
}

static void test_the_ends_are_the_same_node_on_a_single_elem() {
    tda_List l = make_list(1);

    TEST_ASSERT_EQUAL_PTR(tda_list_front_node(&l), tda_list_back_node(&l));
    TEST_ASSERT_NULL(tda_list_node_next(tda_list_front_node(&l)));
    TEST_ASSERT_NULL(tda_list_node_prev(tda_list_front_node(&l)));

    tda_list_deinit(&l);
}

// the mutable backward walk writes through every elem in one pass from the back
static void test_node_prev_mut_writes_through_the_backward_walk() {
    tda_List l = make_list(4);

    size_t seen = 0;
    for (tda_ListNode *node = tda_list_back_node_mut(&l); node; node = tda_list_node_prev_mut(node)) {
        *TDA_LIST_NODE_ELEM_MUT_AS(int32_t, &l, node) *= 10;
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(4, seen);

    constexpr int32_t want[4] = {0, 10, 20, 30};
    assert_elems(&l, want, 4);

    tda_list_deinit(&l);
}

static void test_next_and_prev_are_each_others_inverse() {
    tda_List l = make_list(3);

    const tda_ListNode *head = tda_list_front_node(&l);
    const tda_ListNode *mid = tda_list_node_next(head);
    const tda_ListNode *tail = tda_list_node_next(mid);

    TEST_ASSERT_EQUAL_PTR(tail, tda_list_back_node(&l));
    TEST_ASSERT_NULL(tda_list_node_next(tail));
    TEST_ASSERT_EQUAL_PTR(mid, tda_list_node_prev(tail));
    TEST_ASSERT_EQUAL_PTR(head, tda_list_node_prev(mid));
    TEST_ASSERT_NULL(tda_list_node_prev(head));

    tda_list_deinit(&l);
}

static void test_node_elem_mut_writes_through() {
    tda_List l = make_list(3);

    tda_ListNode *mid = tda_list_node_next_mut(tda_list_front_node_mut(&l));
    *TDA_LIST_NODE_ELEM_MUT_AS(int32_t, &l, mid) = 9;

    assert_elems(&l, (int32_t[]){0, 9, 2}, 3);

    tda_list_deinit(&l);
}

/* ========== push / pop ========== */

static void test_push_back_appends() {
    tda_List l = make_list(0);

    push_int(&l, 1);
    push_int(&l, 2);

    assert_elems(&l, (int32_t[]){1, 2}, 2);

    tda_list_deinit(&l);
}

static void test_push_front_prepends() {
    tda_List l = make_list(0);
    constexpr int32_t one = 1;
    constexpr int32_t two = 2;

    TDA_TEST_OK(tda_list_push_front(&l, &one));
    TDA_TEST_OK(tda_list_push_front(&l, &two));

    assert_elems(&l, (int32_t[]){2, 1}, 2);

    tda_list_deinit(&l);
}

// growing both ends at once keeps the two chains consistent
static void test_push_front_and_back_interleave() {
    tda_List l = make_list(0);

    TDA_TEST_OK(TDA_LIST_PUSH_BACK(int32_t, &l, 1));
    TDA_TEST_OK(TDA_LIST_PUSH_FRONT(int32_t, &l, 0));
    TDA_TEST_OK(TDA_LIST_PUSH_BACK(int32_t, &l, 2));
    TDA_TEST_OK(TDA_LIST_PUSH_FRONT(int32_t, &l, -1));

    assert_elems(&l, (int32_t[]){-1, 0, 1, 2}, 4);

    tda_list_deinit(&l);
}

static void test_pop_front_drops_the_head() {
    tda_List l = make_list(3);

    tda_list_pop_front(&l);

    assert_elems(&l, (int32_t[]){1, 2}, 2);

    tda_list_deinit(&l);
}

static void test_pop_back_drops_the_tail() {
    tda_List l = make_list(3);

    tda_list_pop_back(&l);

    assert_elems(&l, (int32_t[]){0, 1}, 2);

    tda_list_deinit(&l);
}

static void test_pop_to_hands_each_end_out() {
    tda_List l = make_list(4);

    int32_t out = -1;
    tda_list_pop_front_to(&l, &out);
    TEST_ASSERT_EQUAL_INT32(0, out);

    tda_list_pop_back_to(&l, &out);
    TEST_ASSERT_EQUAL_INT32(3, out);

    assert_elems(&l, (int32_t[]){1, 2}, 2);

    tda_list_deinit(&l);
}

// 'out' may be an elem of another node of the same list
static void test_pop_to_into_the_list_itself() {
    tda_List l = make_list(4);

    tda_list_pop_back_to(&l, tda_list_front_mut(&l));
    assert_elems(&l, (int32_t[]){3, 1, 2}, 3);

    tda_list_pop_front_to(&l, tda_list_back_mut(&l));
    assert_elems(&l, (int32_t[]){1, 3}, 2);

    tda_list_deinit(&l);
}

static void test_pop_to_macros_move_whole_elems() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(Pair, tda_al_default(), &l, {1, 2}, {3, 4}, {5, 6}));

    Pair out = {0, 0};
    TDA_LIST_POP_FRONT_TO(Pair, &l, &out);
    TEST_ASSERT_EQUAL_INT64(1, out.a);
    TEST_ASSERT_EQUAL_INT64(2, out.b);

    TDA_LIST_POP_BACK_TO(Pair, &l, &out);
    TEST_ASSERT_EQUAL_INT64(5, out.a);
    TEST_ASSERT_EQUAL_INT64(6, out.b);

    TEST_ASSERT_EQUAL_size_t(1, tda_list_len(&l));

    tda_list_deinit(&l);
}

static void test_popping_to_empty_resets_both_ends() {
    tda_List l = make_list(2);

    tda_list_pop_front(&l);
    tda_list_pop_back(&l);

    assert_empty(&l);

    tda_list_deinit(&l);
}

static void test_push_after_emptying_works() {
    tda_List l = make_list(2);

    tda_list_pop_back(&l);
    tda_list_pop_back(&l);
    push_int(&l, 7);

    assert_elems(&l, (int32_t[]){7}, 1);

    tda_list_deinit(&l);
}

/* ========== insert / remove ========== */

static void test_insert_before_the_middle() {
    tda_List l = make_list(3);
    tda_ListNode *mid = tda_list_node_next_mut(tda_list_front_node_mut(&l));

    TDA_TEST_OK(TDA_LIST_INSERT_BEFORE(int32_t, &l, mid, 9));

    assert_elems(&l, (int32_t[]){0, 9, 1, 2}, 4);

    tda_list_deinit(&l);
}

static void test_insert_after_the_middle() {
    tda_List l = make_list(3);
    tda_ListNode *mid = tda_list_node_next_mut(tda_list_front_node_mut(&l));

    TDA_TEST_OK(TDA_LIST_INSERT_AFTER(int32_t, &l, mid, 9));

    assert_elems(&l, (int32_t[]){0, 1, 9, 2}, 4);

    tda_list_deinit(&l);
}

static void test_insert_before_the_head_moves_the_head() {
    tda_List l = make_list(2);

    TDA_TEST_OK(TDA_LIST_INSERT_BEFORE(int32_t, &l, tda_list_front_node_mut(&l), 9));

    assert_elems(&l, (int32_t[]){9, 0, 1}, 3);

    tda_list_deinit(&l);
}

static void test_insert_after_the_tail_moves_the_tail() {
    tda_List l = make_list(2);

    TDA_TEST_OK(TDA_LIST_INSERT_AFTER(int32_t, &l, tda_list_back_node_mut(&l), 9));

    assert_elems(&l, (int32_t[]){0, 1, 9}, 3);

    tda_list_deinit(&l);
}

// the new node lands where the header says
static void test_the_new_node_is_reachable_from_the_anchor() {
    tda_List l = make_list(3);
    tda_ListNode *mid = tda_list_node_next_mut(tda_list_front_node_mut(&l));

    TDA_TEST_OK(TDA_LIST_INSERT_BEFORE(int32_t, &l, mid, 7));
    TDA_TEST_OK(TDA_LIST_INSERT_AFTER(int32_t, &l, mid, 8));

    TEST_ASSERT_EQUAL_INT32(7, *TDA_LIST_NODE_ELEM_AS(int32_t, &l, tda_list_node_prev(mid)));
    TEST_ASSERT_EQUAL_INT32(8, *TDA_LIST_NODE_ELEM_AS(int32_t, &l, tda_list_node_next(mid)));

    tda_list_deinit(&l);
}

static void test_insert_into_a_single_elem_list() {
    tda_List l = make_list(1);
    tda_ListNode *only = tda_list_front_node_mut(&l);

    TDA_TEST_OK(TDA_LIST_INSERT_BEFORE(int32_t, &l, only, -1));
    TDA_TEST_OK(TDA_LIST_INSERT_AFTER(int32_t, &l, only, 1));

    assert_elems(&l, (int32_t[]){-1, 0, 1}, 3);

    tda_list_deinit(&l);
}

static void test_remove_from_the_middle() {
    tda_List l = make_list(3);
    tda_ListNode *mid = tda_list_node_next_mut(tda_list_front_node_mut(&l));

    tda_list_remove_node(&l, mid);

    assert_elems(&l, (int32_t[]){0, 2}, 2);

    tda_list_deinit(&l);
}

static void test_remove_the_head() {
    tda_List l = make_list(3);

    tda_list_remove_node(&l, tda_list_front_node_mut(&l));

    assert_elems(&l, (int32_t[]){1, 2}, 2);

    tda_list_deinit(&l);
}

static void test_remove_the_tail() {
    tda_List l = make_list(3);

    tda_list_remove_node(&l, tda_list_back_node_mut(&l));

    assert_elems(&l, (int32_t[]){0, 1}, 2);

    tda_list_deinit(&l);
}

static void test_remove_the_only_node_empties_the_list() {
    tda_List l = make_list(1);

    tda_list_remove_node(&l, tda_list_front_node_mut(&l));

    assert_empty(&l);

    tda_list_deinit(&l);
}

/* ========== clear ========== */

static void test_clear_empties_the_list() {
    tda_List l = make_list(3);

    tda_list_clear(&l);

    assert_empty(&l);

    tda_list_deinit(&l);
}

static void test_clear_of_an_empty_list_is_noop() {
    tda_List l = make_list(0);

    tda_list_clear(&l);
    tda_list_clear(&l);

    assert_empty(&l);

    tda_list_deinit(&l);
}

static void test_clear_leaves_the_list_usable() {
    tda_List l = make_list(3);

    tda_list_clear(&l);
    push_int(&l, 7);
    TDA_TEST_OK(TDA_LIST_PUSH_FRONT(int32_t, &l, 6));

    assert_elems(&l, (int32_t[]){6, 7}, 2);

    tda_list_deinit(&l);
}

static void test_clear_frees_every_node() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &l, 1, 2, 3));

    tda_list_clear(&l);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    tda_list_deinit(&l);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== retain ========== */

// every subset of six elems: the kept elems stay in order and keep their nodes
static void test_retain_keeps_exactly_the_passing_nodes_in_order() {
    for (uint32_t mask = 0; mask < 64; ++mask) {
        tda_List l = make_list(6);
        const tda_ListNode *before[6];
        collect_nodes(&l, before, 6);

        tda_list_retain(&l, tda_test_i32_in_mask, &mask);

        int32_t want[6];
        const tda_ListNode *kept[6];
        size_t n = 0;
        for (int32_t i = 0; i < 6; ++i) {
            if (mask >> i & 1) {
                kept[n] = before[i];
                want[n++] = i;
            }
        }
        assert_elems(&l, want, n);

        const tda_ListNode *after[6];
        collect_nodes(&l, after, n);
        for (size_t i = 0; i < n; ++i) {
            TEST_ASSERT_EQUAL_PTR(kept[i], after[i]);
        }

        tda_list_deinit(&l);
    }
}

typedef struct {
    int32_t seen[8];
    size_t calls;
} Calls;

static bool record_and_keep_odd(const void *elem, void *ctx) {
    Calls *calls = ctx;
    const int32_t val = *(const int32_t *) elem;
    calls->seen[calls->calls++] = val;

    return val % 2 != 0;
}

static void test_retain_asks_once_per_elem_front_to_back() {
    tda_List l = make_list(5);

    Calls calls = {0};
    tda_list_retain(&l, record_and_keep_odd, &calls);

    TEST_ASSERT_EQUAL_size_t(5, calls.calls);
    constexpr int32_t order[5] = {0, 1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT32_ARRAY(order, calls.seen, 5);

    tda_list_deinit(&l);
}

static void test_retain_frees_the_dropped_nodes() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &l, 0, 1, 2, 3, 4, 5));

    Calls calls = {0};
    tda_list_retain(&l, record_and_keep_odd, &calls);

    TEST_ASSERT_EQUAL_size_t(3, probe.live); // three nodes

    tda_list_deinit(&l);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== splice ========== */

static void test_splice_back_appends_and_empties_the_source() {
    tda_List dst = make_list(2), src = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &src, 7, 8));

    TDA_TEST_OK(tda_list_splice_back(&dst, &src));

    assert_elems(&dst, (int32_t[]){0, 1, 7, 8}, 4);
    assert_empty(&src);

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
}

static void test_splice_front_prepends_and_empties_the_source() {
    tda_List dst = make_list(2), src = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &src, 7, 8));

    TDA_TEST_OK(tda_list_splice_front(&dst, &src));

    assert_elems(&dst, (int32_t[]){7, 8, 0, 1}, 4);
    assert_empty(&src);

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
}

static void test_splice_into_an_empty_list() {
    tda_List dst = make_list(0), src = make_list(3);

    TDA_TEST_OK(tda_list_splice_back(&dst, &src));

    assert_elems(&dst, (int32_t[]){0, 1, 2}, 3);
    assert_empty(&src);

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
}

static void test_splice_front_into_an_empty_list() {
    tda_List dst = make_list(0), src = make_list(2);

    TDA_TEST_OK(tda_list_splice_front(&dst, &src));

    assert_elems(&dst, (int32_t[]){0, 1}, 2);
    assert_empty(&src);

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
}

static void test_splice_from_an_empty_list_is_noop() {
    tda_List dst = make_list(2), src = make_list(0);

    TDA_TEST_OK(tda_list_splice_back(&dst, &src));
    TDA_TEST_OK(tda_list_splice_front(&dst, &src));

    assert_elems(&dst, (int32_t[]){0, 1}, 2);
    assert_empty(&src);

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
}

// on a shared allocator splice only relinks: no allocation, and the same nodes end up in
// the target
static void test_splice_moves_the_nodes_without_allocating() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List dst = {}, src = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &dst, 1, 2));
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &src, 3, 4));

    const tda_ListNode *src_head = tda_list_front_node(&src);
    const size_t before = tda_test_probe_requests(&probe);

    TDA_TEST_OK(tda_list_splice_back(&dst, &src));

    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_PTR(src_head, tda_list_node_next(tda_list_node_next(tda_list_front_node(&dst))));
    assert_elems(&dst, (int32_t[]){1, 2, 3, 4}, 4);

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// across allocators the nodes cannot move, so the elems are copied into the target's
// allocator and the source is still emptied
static void test_splice_across_allocators_copies_the_elems() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List dst = make_list(2), src = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &src, 7, 8));

    TDA_TEST_OK(tda_list_splice_back(&dst, &src));

    assert_elems(&dst, (int32_t[]){0, 1, 7, 8}, 4);
    assert_empty(&src);
    TEST_ASSERT_EQUAL_size_t(0, probe.live); // src kept nothing

    tda_list_deinit(&src);
    tda_list_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== swap ========== */

static void test_find_returns_the_first_node_holding_the_elem() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 1, 2, 3, 2));

    const tda_ListNode *node = TDA_LIST_FIND(int32_t, &l, 2, tda_eq_i32, nullptr);
    TEST_ASSERT_NOT_NULL(node);

    // the first match: the one whose next holds 3
    TEST_ASSERT_EQUAL_INT32(3, *TDA_LIST_NODE_ELEM_AS(int32_t, &l, tda_list_node_next(node)));

    tda_list_deinit(&l);
}

static void test_find_of_a_missing_elem_is_null() {
    tda_List l = make_list(4);

    TEST_ASSERT_NULL(TDA_LIST_FIND(int32_t, &l, 99, tda_eq_i32, nullptr));

    tda_list_deinit(&l);
}

static void test_find_of_an_empty_list_is_null() {
    tda_List l = make_list(0);

    TEST_ASSERT_NULL(TDA_LIST_FIND(int32_t, &l, 0, tda_eq_i32, nullptr));

    tda_list_deinit(&l);
}

// the position find_mut returns is what insert and remove take
static void test_find_mut_gives_a_position_to_write_through() {
    tda_List l = make_list(4);

    tda_ListNode *at = TDA_LIST_FIND_MUT(int32_t, &l, 2, tda_eq_i32, nullptr);
    TEST_ASSERT_NOT_NULL(at);

    *TDA_LIST_NODE_ELEM_MUT_AS(int32_t, &l, at) = 20;
    TDA_TEST_OK(TDA_LIST_INSERT_BEFORE(int32_t, &l, at, 9));

    assert_elems(&l, (int32_t[]){0, 1, 9, 20, 3}, 5);

    tda_list_remove_node(&l, at);
    assert_elems(&l, (int32_t[]){0, 1, 9, 3}, 4);

    tda_list_deinit(&l);
}

// find matches by the caller's equality
static void test_find_asks_the_equality_it_is_given() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 1, -2, 3));

    TEST_ASSERT_NULL(TDA_LIST_FIND(int32_t, &l, 2, tda_eq_i32, nullptr));
    TEST_ASSERT_NOT_NULL(TDA_LIST_FIND(int32_t, &l, 2, tda_test_i32_eq_abs, nullptr));

    tda_list_deinit(&l);
}

static void test_for_each_walks_front_to_back() {
    tda_List l = make_list(4);

    int32_t seen[4];
    size_t n = 0;
    TDA_LIST_FOR_EACH(node, &l) {
        seen[n++] = *TDA_LIST_NODE_ELEM_AS(int32_t, &l, node);
    }

    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_INT32_ARRAY(((int32_t[]){0, 1, 2, 3}), seen, 4);

    tda_list_deinit(&l);
}

static void test_for_each_over_an_empty_list_runs_no_body() {
    tda_List l = make_list(0);

    size_t n = 0;
    TDA_LIST_FOR_EACH(node, &l) {
        TDA_UNUSED(node);
        ++n;
    }

    TEST_ASSERT_EQUAL_size_t(0, n);

    tda_list_deinit(&l);
}

static void test_for_each_mut_writes_through_every_position() {
    tda_List l = make_list(4);

    TDA_LIST_FOR_EACH_MUT(node, &l) {
        *TDA_LIST_NODE_ELEM_MUT_AS(int32_t, &l, node) *= 10;
    }

    assert_elems(&l, (int32_t[]){0, 10, 20, 30}, 4);

    tda_list_deinit(&l);
}

// the walk takes the next node before the body, so the body may drop the one it is on
static void test_for_each_mut_may_remove_the_node_it_is_on() {
    tda_List l = make_list(6);

    size_t seen = 0;
    TDA_LIST_FOR_EACH_MUT(node, &l) {
        if (*TDA_LIST_NODE_ELEM_AS(int32_t, &l, node) % 2 != 0) {
            tda_list_remove_node(&l, node);
        }
        ++seen;
    }

    TEST_ASSERT_EQUAL_size_t(6, seen);
    assert_elems(&l, (int32_t[]){0, 2, 4}, 3);

    TDA_LIST_FOR_EACH_MUT(node, &l) {
        tda_list_remove_node(&l, node);
    }
    TEST_ASSERT_EQUAL_size_t(0, tda_list_len(&l));

    tda_list_deinit(&l);
}

static void test_swap_exchanges_the_contents() {
    tda_List a = make_list(3), b = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &b, 9));

    tda_list_swap(&a, &b);

    assert_elems(&a, (int32_t[]){9}, 1);
    assert_elems(&b, (int32_t[]){0, 1, 2}, 3);

    tda_list_deinit(&b);
    tda_list_deinit(&a);
}

// the nodes stay put and only change list, so swap is O(1) and borrowed nodes stay valid
static void test_swap_keeps_the_nodes_alive() {
    tda_List a = make_list(2), b = make_list(1);

    const tda_ListNode *a_head = tda_list_front_node(&a), *b_head = tda_list_front_node(&b);

    tda_list_swap(&a, &b);

    TEST_ASSERT_EQUAL_PTR(a_head, tda_list_front_node(&b));
    TEST_ASSERT_EQUAL_PTR(b_head, tda_list_front_node(&a));

    tda_list_deinit(&b);
    tda_list_deinit(&a);
}

static void test_swap_of_itself_changes_nothing() {
    tda_List l = make_list(3);

    tda_list_swap(&l, &l);

    assert_elems(&l, (int32_t[]){0, 1, 2}, 3);

    tda_list_deinit(&l);
}

static void test_swap_with_an_empty_list_works_both_ways() {
    tda_List l = make_list(2);
    tda_List none = make_list(0);

    tda_list_swap(&l, &none);
    assert_empty(&l);
    assert_elems(&none, (int32_t[]){0, 1}, 2);

    tda_list_swap(&l, &none);
    assert_elems(&l, (int32_t[]){0, 1}, 2);
    assert_empty(&none);

    push_int(&none, 7); // both still usable
    assert_elems(&none, (int32_t[]){7}, 1);

    tda_list_deinit(&none);
    tda_list_deinit(&l);
}

/* ========== splice_node ========== */

static void test_splice_node_moves_one_node_between_lists() {
    tda_List a = make_list(3); // 0, 1, 2
    tda_List b = make_list(2); // 0, 1

    TDA_TEST_OK(tda_list_splice_node(&b, node_at(&b, 1), &a, node_at(&a, 1)));

    constexpr int32_t want_a[2] = {0, 2};
    constexpr int32_t want_b[3] = {0, 1, 1};
    assert_elems(&a, want_a, 2);
    assert_elems(&b, want_b, 3);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

// the node itself changes lists, so a pointer held before the move still names it
static void test_splice_node_keeps_the_node_address() {
    tda_List a = make_list(3), b = make_list(1);

    const tda_ListNode *moved = node_at(&a, 2);
    TDA_TEST_OK(tda_list_splice_node(&b, nullptr, &a, node_at(&a, 2)));

    TEST_ASSERT_EQUAL_PTR(moved, tda_list_back_node(&b));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_LIST_NODE_ELEM_AS(int32_t, &b, moved));

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

static void test_splice_node_at_null_appends_to_the_back() {
    tda_List a = make_list(2), b = {}; // 0, 1
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &b, 7, 8));

    TDA_TEST_OK(tda_list_splice_node(&b, nullptr, &a, node_at(&a, 0)));

    constexpr int32_t want_a[1] = {1};
    constexpr int32_t want_b[3] = {7, 8, 0};
    assert_elems(&a, want_a, 1);
    assert_elems(&b, want_b, 3);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

// within one list the node is unlinked before its new neighbour is read, so it cannot
// land next to itself
static void test_splice_node_within_one_list_moves_the_elem() {
    tda_List l = make_list(4); // 0, 1, 2, 3

    TDA_TEST_OK(tda_list_splice_node(&l, node_at(&l, 1), &l, node_at(&l, 3)));

    constexpr int32_t want[4] = {0, 3, 1, 2};
    assert_elems(&l, want, 4);

    tda_list_deinit(&l);
}

static void test_splice_node_to_the_front_of_the_same_list() {
    tda_List l = make_list(3); // 0, 1, 2

    TDA_TEST_OK(tda_list_splice_node(&l, node_at(&l, 0), &l, node_at(&l, 2)));

    constexpr int32_t want[3] = {2, 0, 1};
    assert_elems(&l, want, 3);

    tda_list_deinit(&l);
}

// a move one step forward inside one list: reading the neighbour before unlinking would
// put the node back where it started
static void test_splice_node_one_step_forward() {
    tda_List l = make_list(3); // 0, 1, 2

    TDA_TEST_OK(tda_list_splice_node(&l, node_at(&l, 2), &l, node_at(&l, 1)));

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(&l, want, 3);

    tda_list_deinit(&l);
}

static void test_splice_node_of_the_only_node_empties_the_source() {
    tda_List a = make_list(1), b = make_list(2);

    TDA_TEST_OK(tda_list_splice_node(&b, tda_list_front_node_mut(&b), &a, node_at(&a, 0)));

    assert_empty(&a);
    constexpr int32_t want_b[3] = {0, 0, 1};
    assert_elems(&b, want_b, 3);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

// across allocators a node cannot change owner, so the elem is copied to a new address,
// as in splice_front
static void test_splice_node_across_allocators_copies_the_elem() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_List a = make_list(3), b = {}; // default: 0, 1, 2
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &b, 9));

    const tda_ListNode *before = node_at(&a, 1);
    TDA_TEST_OK(tda_list_splice_node(&b, nullptr, &a, node_at(&a, 1)));

    constexpr int32_t want_a[2] = {0, 2};
    constexpr int32_t want_b[2] = {9, 1};
    assert_elems(&a, want_a, 2);
    assert_elems(&b, want_b, 2);
    TEST_ASSERT_TRUE(before != tda_list_back_node(&b));

    tda_list_deinit(&a);
    tda_list_deinit(&b);
    tda_arena_deinit(&arena);
}

/* ========== reverse ========== */

static void test_reverse_turns_the_list_around() {
    tda_List l = make_list(5); // 0 .. 4

    tda_list_reverse(&l);

    constexpr int32_t want[5] = {4, 3, 2, 1, 0};
    assert_elems(&l, want, 5);

    tda_list_deinit(&l);
}

static void test_reverse_of_empty_and_single_changes_nothing() {
    tda_List empty = make_list(0);
    tda_list_reverse(&empty);
    assert_empty(&empty);
    tda_list_deinit(&empty);

    tda_List one = make_list(1);
    tda_list_reverse(&one);
    constexpr int32_t want[1] = {0};
    assert_elems(&one, want, 1);
    tda_list_deinit(&one);
}

// the same nodes come back in the other order
static void test_reverse_keeps_every_node_address() {
    tda_List l = make_list(4);

    const tda_ListNode *before[4];
    collect_nodes(&l, before, 4);

    tda_list_reverse(&l);

    assert_same_nodes(&l, before, 4);
    TEST_ASSERT_EQUAL_PTR(before[3], tda_list_front_node(&l));
    TEST_ASSERT_EQUAL_PTR(before[0], tda_list_back_node(&l));

    tda_list_deinit(&l);
}

static void test_reverse_twice_is_the_original() {
    tda_List l = make_list(6);

    tda_list_reverse(&l);
    tda_list_reverse(&l);

    constexpr int32_t want[6] = {0, 1, 2, 3, 4, 5};
    assert_elems(&l, want, 6);

    tda_list_deinit(&l);
}

/* ========== sort ========== */

static void test_sort_orders_the_elems() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 5, 1, 9, 3, 7, 2));

    tda_list_sort(&l, tda_cmp_i32, nullptr);

    constexpr int32_t want[6] = {1, 2, 3, 5, 7, 9};
    assert_elems(&l, want, 6);

    tda_list_deinit(&l);
}

static void test_sort_of_empty_and_single_is_a_noop() {
    tda_List empty = make_list(0);
    tda_list_sort(&empty, tda_cmp_i32, nullptr);
    assert_empty(&empty);
    tda_list_deinit(&empty);

    tda_List one = make_list(1);
    tda_list_sort(&one, tda_cmp_i32, nullptr);
    constexpr int32_t want[1] = {0};
    assert_elems(&one, want, 1);
    tda_list_deinit(&one);
}

static void test_sort_handles_duplicates() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 3, 1, 3, 1, 3));

    tda_list_sort(&l, tda_cmp_i32, nullptr);

    constexpr int32_t want[5] = {1, 1, 3, 3, 3};
    assert_elems(&l, want, 5);

    tda_list_deinit(&l);
}

static void test_sort_takes_a_descending_comparator() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 5, 1, 9, 3));

    tda_list_sort(&l, tda_cmp_desc_i32, nullptr);

    constexpr int32_t want[4] = {9, 5, 3, 1};
    assert_elems(&l, want, 4);

    tda_list_deinit(&l);
}

// each length up to 17 gives a different merge sort split, and odd lengths tell len/2
// from (len + 1)/2
static void test_sort_works_at_every_length() {
    for (size_t n = 0; n <= 17; ++n) {
        tda_List l = TDA_LIST_INIT(int32_t, tda_al_default());

        // a descending run, the worst case for a sort that assumes an order
        for (size_t i = 0; i < n; ++i) {
            push_int(&l, (int32_t) (n - i));
        }

        tda_list_sort(&l, tda_cmp_i32, nullptr);

        TEST_ASSERT_EQUAL_size_t(n, tda_list_len(&l));
        int32_t prev = 0;
        size_t seen = 0;
        for (const tda_ListNode *node = tda_list_front_node(&l); node; node = tda_list_node_next(node)) {
            const int32_t val = *TDA_LIST_NODE_ELEM_AS(int32_t, &l, node);
            if (seen > 0) {
                TEST_ASSERT_TRUE_MESSAGE(prev <= val, "the sorted list is out of order");
            }
            prev = val;
            ++seen;
        }
        TEST_ASSERT_EQUAL_size_t(n, seen);

        tda_list_deinit(&l);
    }
}

// equal keys keep their push order, which the second field shows
static void test_sort_is_stable() {
    tda_List l = TDA_LIST_INIT(Pair, tda_al_default());

    constexpr Pair src[6] = {{2, 1}, {1, 1}, {2, 2}, {1, 2}, {2, 3}, {1, 3}};
    for (size_t i = 0; i < 6; ++i) {
        TDA_TEST_OK(tda_list_push_back(&l, &src[i]));
    }

    tda_list_sort(&l, tda_test_pair_cmp_a, nullptr);

    constexpr Pair want[6] = {{1, 1}, {1, 2}, {1, 3}, {2, 1}, {2, 2}, {2, 3}};
    size_t i = 0;
    for (const tda_ListNode *node = tda_list_front_node(&l); node; node = tda_list_node_next(node)) {
        const Pair *got = TDA_LIST_NODE_ELEM_AS(Pair, &l, node);
        TEST_ASSERT_EQUAL_INT64(want[i].a, got->a);
        TEST_ASSERT_EQUAL_INT64(want[i].b, got->b);
        ++i;
    }
    TEST_ASSERT_EQUAL_size_t(6, i);

    tda_list_deinit(&l);
}

// the nodes move and each elem stays in its node, so a pointer taken before the sort
// still names the same elem
static void test_sort_keeps_every_node_and_its_elem() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 5, 1, 9, 3));

    const tda_ListNode *before[4];
    collect_nodes(&l, before, 4);
    const tda_ListNode *node_of_nine = before[2];

    tda_list_sort(&l, tda_cmp_i32, nullptr);

    assert_same_nodes(&l, before, 4);
    TEST_ASSERT_EQUAL_INT32(9, *TDA_LIST_NODE_ELEM_AS(int32_t, &l, node_of_nine));
    TEST_ASSERT_EQUAL_PTR(node_of_nine, tda_list_back_node(&l));

    tda_list_deinit(&l);
}

// relinking never allocates
static void test_sort_never_allocates() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &l, 5, 1, 9, 3, 7, 2, 8));

    const size_t requests = tda_test_probe_requests(&probe);
    tda_list_sort(&l, tda_cmp_i32, nullptr);

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    constexpr int32_t want[7] = {1, 2, 3, 5, 7, 8, 9};
    assert_elems(&l, want, 7);

    tda_list_deinit(&l);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== merge ========== */

static void test_merge_interleaves_two_sorted_lists() {
    tda_List a = {}, b = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &a, 1, 4, 7));
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &b, 2, 3, 8, 9));

    TDA_TEST_OK(tda_list_merge(&a, &b, tda_cmp_i32, nullptr));

    constexpr int32_t want[7] = {1, 2, 3, 4, 7, 8, 9};
    assert_elems(&a, want, 7);
    assert_empty(&b);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

static void test_merge_with_an_empty_source_changes_nothing() {
    tda_List a = make_list(3), b = make_list(0);

    TDA_TEST_OK(tda_list_merge(&a, &b, tda_cmp_i32, nullptr));

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(&a, want, 3);
    assert_empty(&b);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

static void test_merge_into_an_empty_list_takes_everything() {
    tda_List a = make_list(0), b = make_list(4);

    TDA_TEST_OK(tda_list_merge(&a, &b, tda_cmp_i32, nullptr));

    constexpr int32_t want[4] = {0, 1, 2, 3};
    assert_elems(&a, want, 4);
    assert_empty(&b);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

// equal elems of 'self' come before those of 'src', as in algo/merge
static void test_merge_keeps_equal_elems_of_self_first() {
    tda_List a = TDA_LIST_INIT(Pair, tda_al_default()), b = TDA_LIST_INIT(Pair, tda_al_default());

    constexpr Pair from_a[2] = {{1, 100}, {2, 100}};
    constexpr Pair from_b[2] = {{1, 200}, {2, 200}};
    for (size_t i = 0; i < 2; ++i) {
        TDA_TEST_OK(tda_list_push_back(&a, &from_a[i]));
        TDA_TEST_OK(tda_list_push_back(&b, &from_b[i]));
    }

    TDA_TEST_OK(tda_list_merge(&a, &b, tda_test_pair_cmp_a, nullptr));

    constexpr Pair want[4] = {{1, 100}, {1, 200}, {2, 100}, {2, 200}};
    size_t i = 0;
    for (const tda_ListNode *node = tda_list_front_node(&a); node; node = tda_list_node_next(node)) {
        const Pair *got = TDA_LIST_NODE_ELEM_AS(Pair, &a, node);
        TEST_ASSERT_EQUAL_INT64(want[i].a, got->a);
        TEST_ASSERT_EQUAL_INT64(want[i].b, got->b);
        ++i;
    }
    TEST_ASSERT_EQUAL_size_t(4, i);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

static void test_merge_on_one_allocator_never_allocates() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_List a = {}, b = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &a, 1, 4, 7));
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &b, 2, 3));

    const size_t requests = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_list_merge(&a, &b, tda_cmp_i32, nullptr));

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    constexpr int32_t want[5] = {1, 2, 3, 4, 7};
    assert_elems(&a, want, 5);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// across allocators the elems are copied into nodes of 'self' and 'src' is still emptied
static void test_merge_across_allocators_copies_the_elems() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_List a = {}, b = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &a, 1, 5));
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &b, 2, 9));

    TDA_TEST_OK(tda_list_merge(&a, &b, tda_cmp_i32, nullptr));

    constexpr int32_t want[4] = {1, 2, 5, 9};
    assert_elems(&a, want, 4);
    assert_empty(&b);
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_list_al(&a)));

    tda_list_deinit(&a);
    tda_list_deinit(&b);
    tda_arena_deinit(&arena);
}

/* ========== copy to span ========== */

static void test_copy_to_span_writes_front_to_back() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 5, 1, 9));

    int32_t got[3];
    tda_list_copy_to_span(&l, TDA_SPAN_FROM_DATA_MUT(int32_t, got, 3));

    constexpr int32_t want[3] = {5, 1, 9};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, got, 3);
    assert_elems(&l, want, 3);

    tda_list_deinit(&l);
}

static void test_copy_to_span_of_empty_writes_nothing() {
    tda_List l = make_list(0);

    int32_t got[2] = {11, 22};
    tda_list_copy_to_span(&l, TDA_SPAN_FROM_DATA_MUT(int32_t, got, 0));

    TEST_ASSERT_EQUAL_INT32(11, got[0]);
    TEST_ASSERT_EQUAL_INT32(22, got[1]);

    tda_list_deinit(&l);
}

static void test_copy_from_span_overwrites_every_elem() {
    tda_List l = make_list(3); // 0, 1, 2

    constexpr int32_t src[3] = {7, 8, 9};
    tda_list_copy_from_span(&l, TDA_SPAN_FROM_DATA(int32_t, src, 3));

    assert_elems(&l, src, 3);

    tda_list_deinit(&l);
}

// the pair writes through the existing nodes, so nothing is allocated and every
// borrowed node stays in its list
static void test_copy_from_span_keeps_the_nodes() {
    tda_List l = make_list(3);

    const tda_ListNode *before[3];
    collect_nodes(&l, before, 3);

    constexpr int32_t src[3] = {7, 8, 9};
    tda_list_copy_from_span(&l, TDA_SPAN_FROM_DATA(int32_t, src, 3));

    assert_same_nodes(&l, before, 3);
    TEST_ASSERT_EQUAL_INT32(7, *TDA_LIST_NODE_ELEM_AS(int32_t, &l, before[0]));

    tda_list_deinit(&l);
}

// unlike tda_list_sort, the round trip moves elems between nodes, so the node that held
// 9 now holds something else
static void test_the_copy_reaches_algo_and_comes_back() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 5, 1, 9, 3));

    const tda_ListNode *node_of_nine = node_at(&l, 2);

    int32_t buf[4];
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);
    tda_list_copy_to_span(&l, s);

    TEST_ASSERT_EQUAL_size_t(2, tda_span_max_elem(tda_span_mut_to_span(s), tda_cmp_i32, nullptr));

    tda_span_sort(s, tda_cmp_i32, nullptr);
    tda_list_copy_from_span(&l, tda_span_mut_to_span(s));

    constexpr int32_t want[4] = {1, 3, 5, 9};
    assert_elems(&l, want, 4);
    TEST_ASSERT_EQUAL_INT32(5, *TDA_LIST_NODE_ELEM_AS(int32_t, &l, node_of_nine));

    tda_list_deinit(&l);
}

/* ========== allocation failure ========== */

static void test_push_back_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &l, 1, 2));
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_LIST_PUSH_BACK(int32_t, &l, 3));

    assert_elems(&l, (int32_t[]){1, 2}, 2); // the list is exactly as it was

    tda_arena_deinit(&arena);
}

static void test_push_front_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &l, 1, 2));
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_LIST_PUSH_FRONT(int32_t, &l, 0));

    assert_elems(&l, (int32_t[]){1, 2}, 2);

    tda_arena_deinit(&arena);
}

static void test_insert_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &l, 1, 2));
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_LIST_INSERT_AFTER(int32_t, &l, tda_list_front_node_mut(&l), 9));

    assert_elems(&l, (int32_t[]){1, 2}, 2);

    tda_arena_deinit(&arena);
}

static void test_extend_appends_in_order() {
    tda_List l = make_list(2);

    TDA_TEST_OK(tda_list_extend(&l, TDA_SPAN_OF(int32_t, 7, 8, 9)));
    assert_elems(&l, (int32_t[]){0, 1, 7, 8, 9}, 5);

    TDA_TEST_OK(tda_list_extend(&l, TDA_SPAN_FROM_DATA(int32_t, nullptr, 0)));
    TEST_ASSERT_EQUAL_size_t(5, tda_list_len(&l));

    tda_list_deinit(&l);
}

static void test_extend_an_empty_list_sets_both_ends() {
    tda_List l = TDA_LIST_INIT(int32_t, tda_al_default());

    TDA_TEST_OK(TDA_LIST_EXTEND(int32_t, &l, 4, 5));
    assert_elems(&l, (int32_t[]){4, 5}, 2);
    TEST_ASSERT_EQUAL_INT32(4, *TDA_LIST_FRONT_AS(int32_t, &l));
    TEST_ASSERT_EQUAL_INT32(5, *TDA_LIST_BACK_AS(int32_t, &l));

    tda_list_deinit(&l);
}

static void test_extend_macro_takes_whole_elems() {
    tda_List l = TDA_LIST_INIT(Pair, tda_al_default());

    TDA_TEST_OK(TDA_LIST_EXTEND(Pair, &l, {1, 2}, {3, 4}));

    const Pair *back = TDA_LIST_BACK_AS(Pair, &l);
    TEST_ASSERT_EQUAL_INT64(3, back->a);
    TEST_ASSERT_EQUAL_INT64(4, back->b);

    tda_list_deinit(&l);
}

// all or nothing: the nodes made before the refusal go back, and the list is as it was
static void test_extend_rolls_back_the_partial_chain() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &l, 1, 2));
    const size_t live = probe.live;

    tda_test_probe_fail_after_next(&probe, 2); // two nodes, then refuse the third
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_LIST_EXTEND(int32_t, &l, 3, 4, 5));

    assert_elems(&l, (int32_t[]){1, 2}, 2);
    TEST_ASSERT_EQUAL_size_t(live, probe.live);

    probe.fail_after = SIZE_MAX;
    tda_list_deinit(&l);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a from_data that fails halfway must not leak the nodes it built; an arena cannot show
// that (its dealloc is a no-op), a probe can
static void test_from_data_rolls_back_the_partial_list() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    constexpr int32_t src[] = {1, 2, 3, 4};
    tda_List l = {};
    tda_test_probe_fail_after_next(&probe, 3); // three nodes, then refuse the fourth

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_LIST_FROM_DATA(int32_t, src, 4, al, &l));

    TEST_ASSERT_NULL(l.al_.vt);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_copy_rolls_back_the_partial_copy() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &l, 1, 2, 3));
    const size_t live = probe.live;

    tda_test_probe_fail_after_next(&probe, 2); // two nodes, then refuse the third
    tda_List c;
    TDA_TEST_POISON(c);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_list_copy(&l, &c));

    TDA_TEST_ASSERT_POISONED(c);
    TEST_ASSERT_EQUAL_size_t(live, probe.live); // nothing of the clone survived

    probe.fail_after = SIZE_MAX;
    tda_list_deinit(&l);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the target allocates only for elems it has no node for, so the failing case is a
// target SHORTER than the source
static void test_copy_assign_reports_a_refused_block_and_changes_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List src = make_list(4), dst = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al, &dst, 7, 8));
    const size_t live = probe.live;

    tda_test_probe_fail_after_next(&probe, 1); // one node of the two it needs
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_list_copy_assign(&dst, &src));

    assert_elems(&dst, (int32_t[]){7, 8}, 2);
    TEST_ASSERT_EQUAL_size_t(live, probe.live);

    probe.fail_after = SIZE_MAX;
    tda_list_deinit(&dst);
    tda_list_deinit(&src);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_splice_across_allocators_reports_a_refused_block() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_List dst = TDA_LIST_INIT(int32_t, al);
    tda_List src = make_list(3);

    tda_test_probe_fail_after_next(&probe, 1); // one node of the clone, then refuse
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_list_splice_back(&dst, &src));

    assert_elems(&src, (int32_t[]){0, 1, 2}, 3); // the source keeps its nodes
    assert_empty(&dst);

    probe.fail_after = SIZE_MAX;
    tda_list_deinit(&dst);
    tda_list_deinit(&src);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// only the copy path can fail: an exhausted arena refuses the node
static void test_splice_node_across_allocators_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    tda_List a = make_list(3), b = {}; // default: 0, 1, 2
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &b, 9));
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_list_splice_node(&b, nullptr, &a, node_at(&a, 1)));

    constexpr int32_t want_a[3] = {0, 1, 2};
    constexpr int32_t want_b[1] = {9};
    assert_elems(&a, want_a, 3);
    assert_elems(&b, want_b, 1);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
    tda_arena_deinit(&arena);
}

static void test_merge_across_allocators_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    tda_List a = {}, b = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_arena_to_al(&arena), &a, 1, 5));
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &b, 2, 9));
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_list_merge(&a, &b, tda_cmp_i32, nullptr));

    constexpr int32_t want_a[2] = {1, 5};
    constexpr int32_t want_b[2] = {2, 9};
    assert_elems(&a, want_a, 2);
    assert_elems(&b, want_b, 2);

    tda_list_deinit(&a);
    tda_list_deinit(&b);
    tda_arena_deinit(&arena);
}

/* ========== macros ========== */

static void test_macro_of_builds_from_a_value_list() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 4, 5, 6));

    assert_elems(&l, (int32_t[]){4, 5, 6}, 3);

    tda_list_deinit(&l);
}

static void test_macro_of_carries_wide_elems() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(Pair, tda_al_default(), &l, {1, 2}, {3, 4}));

    TEST_ASSERT_EQUAL_size_t(2, tda_list_len(&l));
    TEST_ASSERT_EQUAL_INT64(2, TDA_LIST_FRONT_AS(Pair, &l)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_LIST_BACK_AS(Pair, &l)->a);

    tda_list_deinit(&l);
}

static void test_macro_push_evaluates_its_value_once() {
    tda_List l = make_list(0);
    int32_t next = 0;

    for (int i = 0; i < 3; ++i) {
        TDA_TEST_OK(TDA_LIST_PUSH_BACK(int32_t, &l, next++));
    }

    TEST_ASSERT_EQUAL_INT32(3, next);
    assert_elems(&l, (int32_t[]){0, 1, 2}, 3);

    tda_list_deinit(&l);
}

static void test_macro_node_elem_as_reads_and_writes() {
    tda_List l = make_list(2);
    tda_ListNode *head = tda_list_front_node_mut(&l);

    TEST_ASSERT_EQUAL_INT32(0, *TDA_LIST_NODE_ELEM_AS(int32_t, &l, head));
    *TDA_LIST_NODE_ELEM_MUT_AS(int32_t, &l, head) = 5;

    assert_elems(&l, (int32_t[]){5, 1}, 2);

    tda_list_deinit(&l);
}

/* ========== compare ========== */

static void test_eq_matches_the_same_elems() {
    tda_List a = make_list(4), b = make_list(4);

    TEST_ASSERT_TRUE(tda_list_eq(&a, &a));
    TEST_ASSERT_TRUE(tda_list_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_list_eq(&b, &a));
    TEST_ASSERT_TRUE(tda_list_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

static void test_eq_parts_one_differing_elem() {
    tda_List a = make_list(4), b = make_list(4);
    TDA_TEST_OK(TDA_LIST_PUSH_BACK(int32_t, &b, 99));
    tda_list_pop_front(&b);

    TEST_ASSERT_FALSE(tda_list_eq(&a, &b));
    TEST_ASSERT_FALSE(tda_list_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

static void test_eq_parts_different_lengths() {
    tda_List a = make_list(4);
    tda_List shorter = make_list(3);

    TEST_ASSERT_FALSE(tda_list_eq(&a, &shorter));
    TEST_ASSERT_FALSE(tda_list_eq(&shorter, &a));

    tda_list_deinit(&a);
    tda_list_deinit(&shorter);
}

static void test_eq_of_two_empties() {
    tda_List a = make_list(0);
    tda_List b = make_list(0);
    tda_List one = make_list(1);

    TEST_ASSERT_TRUE(tda_list_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_list_eq_by(&a, &b, tda_eq_i32, nullptr));
    TEST_ASSERT_FALSE(tda_list_eq(&a, &one));

    tda_list_deinit(&a);
    tda_list_deinit(&b);
    tda_list_deinit(&one);
}

// the same elems in another order are different contents, so the walk compares in order
static void test_eq_is_order_sensitive() {
    tda_List a = make_list(4), b = make_list(4);

    tda_list_reverse(&b);
    TEST_ASSERT_FALSE(tda_list_eq(&a, &b));

    tda_list_reverse(&b);
    TEST_ASSERT_TRUE(tda_list_eq(&a, &b));

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

static void test_eq_by_asks_the_equality() {
    constexpr Pair lhs[2] = {{1, 10}, {2, 20}};
    constexpr Pair rhs[2] = {{1, 70}, {2, 80}};

    tda_List a = {}, b = {};
    TDA_TEST_OK(TDA_LIST_FROM_DATA(Pair, lhs, 2, tda_al_default(), &a));
    TDA_TEST_OK(TDA_LIST_FROM_DATA(Pair, rhs, 2, tda_al_default(), &b));

    TEST_ASSERT_FALSE(tda_list_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_list_eq_by(&a, &b, tda_test_pair_eq_a, nullptr));

    tda_list_deinit(&a);
    tda_list_deinit(&b);
}

/* ========== print ========== */

// printers write to a stream, so cases read it back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_List *l) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_list_fprint(l, stream, tda_fprint_i32, nullptr);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_elems() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 5, 3, 1));

    assert_prints("[5, 3, 1]", &l);

    tda_list_deinit(&l);
}

static void test_fprint_of_a_single_elem_has_no_separator() {
    tda_List l = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &l, 7));

    assert_prints("[7]", &l);

    tda_list_deinit(&l);
}

static void test_fprint_of_an_empty_list() {
    tda_List l = make_list(0);

    assert_prints("[]", &l);

    tda_list_deinit(&l);
}

// stdout cannot be captured portably, so this only checks that the stdout variant runs
static void test_print_writes_to_stdout() {
    tda_List l = make_list(3);

    tda_list_print(&l, tda_fprint_i32, nullptr);

    tda_list_deinit(&l);
}

// each side keeps the allocator its elems came from, and each is given back to it
static void test_swap_across_allocators_carries_each_allocator() {
    tda_TestProbe pa, pb;
    tda_test_probe_reset(&pa);
    tda_test_probe_reset(&pb);
    tda_Al al_a = tda_test_probe_full(&pa), al_b = tda_test_probe_full(&pb);

    tda_List a = {}, b = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al_a, &a, 1, 2));
    TDA_TEST_OK(TDA_LIST_OF(int32_t, al_b, &b, 3, 4, 5));

    tda_list_swap(&a, &b);

    TEST_ASSERT_TRUE(tda_al_eq(al_b, tda_list_al(&a)));
    TEST_ASSERT_TRUE(tda_al_eq(al_a, tda_list_al(&b)));
    TEST_ASSERT_EQUAL_size_t(3, tda_list_len(&a));

    tda_list_deinit(&a);
    TEST_ASSERT_EQUAL_size_t(0, pb.live);
    tda_list_deinit(&b);
    TEST_ASSERT_EQUAL_size_t(0, pa.live);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_List x = {};
    TDA_TEST_OK(TDA_LIST_OF(int32_t, tda_al_default(), &x, 1, 2));

    tda_list_deinit(&x);
    TEST_ASSERT_NULL(x.al_.vt);

    tda_list_deinit(&x);
}


typedef struct {
    int32_t score;
    int64_t id;
} Rec;

// 'ctx' is the key the find was given: it must arrive on the right, never as an elem
static bool eq_rec_by_id(const void *elem, const void *key, void *ctx) {
    TEST_ASSERT_EQUAL_PTR(ctx, key);

    return ((const Rec *) elem)->id == *(const int64_t *) key;
}

static void test_find_takes_a_key_of_another_type() {
    tda_List l = TDA_LIST_INIT(Rec, tda_al_default());
    TDA_TEST_OK(tda_list_push_back(&l, &(Rec){1, 10}));
    TDA_TEST_OK(tda_list_push_back(&l, &(Rec){2, 20}));
    int64_t twenty = 20;

    const tda_ListNode *node = tda_list_find(&l, &twenty, eq_rec_by_id, &twenty);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT32(2, ((const Rec *) tda_list_node_elem(&l, node))->score);

    tda_list_deinit(&l);
}


/* ========== print errors ========== */

// the ctx reaches every call, every elem is still asked after a failure, and the first
// failure is what comes back; print adds only its newline
static void test_fprint_hands_on_the_ctx_and_the_first_failure() {
    tda_List l = make_list(3);
    tda_TestPrint pr = {.fail_from = 2};
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    TEST_ASSERT_EQUAL_INT(-2, tda_list_fprint(&l, stream, tda_test_fprint_i32_failing, &pr));
    TEST_ASSERT_EQUAL_size_t(3, pr.calls);
    fclose(stream);

    pr = (tda_TestPrint){.fail_from = 0};
    TEST_ASSERT_EQUAL_INT(0, tda_list_print(&l, tda_test_fprint_i32_failing, &pr));
    pr = (tda_TestPrint){.fail_from = 2};
    TEST_ASSERT_EQUAL_INT(-2, tda_list_print(&l, tda_test_fprint_i32_failing, &pr));

    FILE *ro = tda_test_unwritable_stream();
    TEST_ASSERT_LESS_THAN_INT(0, tda_list_fprint(&l, ro, tda_fprint_i32, nullptr));
    fclose(ro);

    tda_list_deinit(&l);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_empty);
    RUN_TEST(test_init_keeps_the_elem_layout_and_allocator);
    RUN_TEST(test_node_layout_fits_a_pool_to_the_list);
    RUN_TEST(test_node_layout_grows_with_the_elem);
    RUN_TEST(test_from_data_copies_and_detaches_the_source);
    RUN_TEST(test_from_data_empty_is_empty);
    RUN_TEST(test_from_data_copies_whole_elems);
    RUN_TEST(test_from_span_copies_the_view);
    RUN_TEST(test_deinit_of_an_inert_value_is_noop);
    RUN_TEST(test_deinit_twice_is_noop);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_inherits_the_source_allocator);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_hands_the_allocator_over);
    RUN_TEST(test_move_assign_needs_no_memory);
    RUN_TEST(test_move_assign_fills_an_inert_target);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_of_empty_is_empty);
    RUN_TEST(test_copy_assign_grows_the_target);
    RUN_TEST(test_copy_assign_shrinks_the_target);
    RUN_TEST(test_copy_assign_from_empty_empties_the_target);
    RUN_TEST(test_copy_assign_to_empty_target);
    RUN_TEST(test_copy_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_assign_keeps_the_target_allocator);
    RUN_TEST(test_copy_assign_reuses_the_target_nodes);

    RUN_TEST(test_len_counts_the_elems);
    RUN_TEST(test_elem_layout_is_the_init_arg);
    RUN_TEST(test_al_is_the_init_arg);

    RUN_TEST(test_first_and_last_see_both_ends);
    RUN_TEST(test_first_and_last_meet_on_a_single_elem);
    RUN_TEST(test_first_mut_writes_through);
    RUN_TEST(test_last_mut_writes_through);

    RUN_TEST(test_nodes_of_an_empty_list_are_null);
    RUN_TEST(test_the_ends_are_the_same_node_on_a_single_elem);
    RUN_TEST(test_node_prev_mut_writes_through_the_backward_walk);
    RUN_TEST(test_next_and_prev_are_each_others_inverse);
    RUN_TEST(test_node_elem_mut_writes_through);

    RUN_TEST(test_push_back_appends);
    RUN_TEST(test_push_front_prepends);
    RUN_TEST(test_push_front_and_back_interleave);
    RUN_TEST(test_pop_front_drops_the_head);
    RUN_TEST(test_pop_back_drops_the_tail);
    RUN_TEST(test_pop_to_hands_each_end_out);
    RUN_TEST(test_pop_to_into_the_list_itself);
    RUN_TEST(test_pop_to_macros_move_whole_elems);
    RUN_TEST(test_popping_to_empty_resets_both_ends);
    RUN_TEST(test_push_after_emptying_works);

    RUN_TEST(test_insert_before_the_middle);
    RUN_TEST(test_insert_after_the_middle);
    RUN_TEST(test_insert_before_the_head_moves_the_head);
    RUN_TEST(test_insert_after_the_tail_moves_the_tail);
    RUN_TEST(test_the_new_node_is_reachable_from_the_anchor);
    RUN_TEST(test_insert_into_a_single_elem_list);
    RUN_TEST(test_remove_from_the_middle);
    RUN_TEST(test_remove_the_head);
    RUN_TEST(test_remove_the_tail);
    RUN_TEST(test_remove_the_only_node_empties_the_list);

    RUN_TEST(test_clear_empties_the_list);
    RUN_TEST(test_clear_of_an_empty_list_is_noop);
    RUN_TEST(test_clear_leaves_the_list_usable);
    RUN_TEST(test_clear_frees_every_node);

    RUN_TEST(test_retain_keeps_exactly_the_passing_nodes_in_order);
    RUN_TEST(test_retain_asks_once_per_elem_front_to_back);
    RUN_TEST(test_retain_frees_the_dropped_nodes);

    RUN_TEST(test_splice_back_appends_and_empties_the_source);
    RUN_TEST(test_splice_front_prepends_and_empties_the_source);
    RUN_TEST(test_splice_into_an_empty_list);
    RUN_TEST(test_splice_front_into_an_empty_list);
    RUN_TEST(test_splice_from_an_empty_list_is_noop);
    RUN_TEST(test_splice_moves_the_nodes_without_allocating);
    RUN_TEST(test_splice_across_allocators_copies_the_elems);

    RUN_TEST(test_find_returns_the_first_node_holding_the_elem);
    RUN_TEST(test_find_of_a_missing_elem_is_null);
    RUN_TEST(test_find_of_an_empty_list_is_null);
    RUN_TEST(test_find_mut_gives_a_position_to_write_through);
    RUN_TEST(test_find_asks_the_equality_it_is_given);
    RUN_TEST(test_for_each_walks_front_to_back);
    RUN_TEST(test_for_each_over_an_empty_list_runs_no_body);
    RUN_TEST(test_for_each_mut_writes_through_every_position);
    RUN_TEST(test_for_each_mut_may_remove_the_node_it_is_on);

    RUN_TEST(test_swap_exchanges_the_contents);
    RUN_TEST(test_swap_keeps_the_nodes_alive);
    RUN_TEST(test_swap_of_itself_changes_nothing);
    RUN_TEST(test_swap_with_an_empty_list_works_both_ways);

    RUN_TEST(test_splice_node_moves_one_node_between_lists);
    RUN_TEST(test_splice_node_keeps_the_node_address);
    RUN_TEST(test_splice_node_at_null_appends_to_the_back);
    RUN_TEST(test_splice_node_within_one_list_moves_the_elem);
    RUN_TEST(test_splice_node_to_the_front_of_the_same_list);
    RUN_TEST(test_splice_node_one_step_forward);
    RUN_TEST(test_splice_node_of_the_only_node_empties_the_source);
    RUN_TEST(test_splice_node_across_allocators_copies_the_elem);

    RUN_TEST(test_reverse_turns_the_list_around);
    RUN_TEST(test_reverse_of_empty_and_single_changes_nothing);
    RUN_TEST(test_reverse_keeps_every_node_address);
    RUN_TEST(test_reverse_twice_is_the_original);

    RUN_TEST(test_sort_orders_the_elems);
    RUN_TEST(test_sort_of_empty_and_single_is_a_noop);
    RUN_TEST(test_sort_handles_duplicates);
    RUN_TEST(test_sort_takes_a_descending_comparator);
    RUN_TEST(test_sort_works_at_every_length);
    RUN_TEST(test_sort_is_stable);
    RUN_TEST(test_sort_keeps_every_node_and_its_elem);
    RUN_TEST(test_sort_never_allocates);

    RUN_TEST(test_merge_interleaves_two_sorted_lists);
    RUN_TEST(test_merge_with_an_empty_source_changes_nothing);
    RUN_TEST(test_merge_into_an_empty_list_takes_everything);
    RUN_TEST(test_merge_keeps_equal_elems_of_self_first);
    RUN_TEST(test_merge_on_one_allocator_never_allocates);
    RUN_TEST(test_merge_across_allocators_copies_the_elems);

    RUN_TEST(test_copy_to_span_writes_front_to_back);
    RUN_TEST(test_copy_to_span_of_empty_writes_nothing);
    RUN_TEST(test_copy_from_span_overwrites_every_elem);
    RUN_TEST(test_copy_from_span_keeps_the_nodes);
    RUN_TEST(test_the_copy_reaches_algo_and_comes_back);
    RUN_TEST(test_push_back_reports_an_exhausted_arena);
    RUN_TEST(test_push_front_reports_an_exhausted_arena);
    RUN_TEST(test_insert_reports_an_exhausted_arena);
    RUN_TEST(test_from_data_rolls_back_the_partial_list);
    RUN_TEST(test_extend_appends_in_order);
    RUN_TEST(test_extend_an_empty_list_sets_both_ends);
    RUN_TEST(test_extend_macro_takes_whole_elems);
    RUN_TEST(test_extend_rolls_back_the_partial_chain);
    RUN_TEST(test_copy_rolls_back_the_partial_copy);
    RUN_TEST(test_copy_assign_reports_a_refused_block_and_changes_nothing);
    RUN_TEST(test_splice_across_allocators_reports_a_refused_block);
    RUN_TEST(test_splice_node_across_allocators_reports_an_exhausted_arena);
    RUN_TEST(test_merge_across_allocators_reports_an_exhausted_arena);

    RUN_TEST(test_macro_of_builds_from_a_value_list);
    RUN_TEST(test_macro_of_carries_wide_elems);
    RUN_TEST(test_macro_push_evaluates_its_value_once);
    RUN_TEST(test_macro_node_elem_as_reads_and_writes);


    RUN_TEST(test_eq_matches_the_same_elems);
    RUN_TEST(test_eq_parts_one_differing_elem);
    RUN_TEST(test_eq_parts_different_lengths);
    RUN_TEST(test_eq_of_two_empties);
    RUN_TEST(test_eq_is_order_sensitive);
    RUN_TEST(test_eq_by_asks_the_equality);

    RUN_TEST(test_fprint_writes_the_elems);
    RUN_TEST(test_fprint_of_a_single_elem_has_no_separator);
    RUN_TEST(test_fprint_of_an_empty_list);
    RUN_TEST(test_print_writes_to_stdout);

    RUN_TEST(test_swap_across_allocators_carries_each_allocator);

    RUN_TEST(test_find_takes_a_key_of_another_type);

    RUN_TEST(test_fprint_hands_on_the_ctx_and_the_first_failure);

    return UNITY_END();
}
