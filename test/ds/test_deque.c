#include "tda/ds/deque.h"
#include "tda/algo/sort.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
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

/* ========== helpers ========== */

// The contents wrap exactly when the front sits at a higher address than the back.
// Both point into one block, so the comparison is valid, and a test can assert that its
// ring really is split.
[[nodiscard]]
static bool wraps(const tda_Deque *d) {
    return (const unsigned char *) tda_deque_front(d) > (const unsigned char *) tda_deque_back(d);
}

static void assert_elems(const tda_Deque *d, const int32_t *want, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, tda_deque_len(d));

    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_EQUAL_INT32(want[i], *TDA_DEQUE_GET_AS(int32_t, d, i));
    }
}

static void push_back_int(tda_Deque *d, int32_t val) {
    TDA_TEST_OK(tda_deque_push_back(d, &val));
}

static void push_front_int(tda_Deque *d, int32_t val) {
    TDA_TEST_OK(tda_deque_push_front(d, &val));
}

// int32_t deque holding 0, 1, ... len-1, filled from the back
[[nodiscard]]
static tda_Deque make_deque(size_t len) {
    tda_Deque d = TDA_DEQUE_INIT(int32_t, tda_al_default());

    for (size_t i = 0; i < len; ++i) {
        push_back_int(&d, (int32_t) i);
    }

    return d;
}

// {10, 20, 30, 40} in a ring of exactly four slots, always split: two elems are popped
// off the front and two more wrap around onto the back
[[nodiscard]]
static tda_Deque make_wrapped() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 4, tda_al_default(), &d));

    push_back_int(&d, 1);
    push_back_int(&d, 2);
    push_back_int(&d, 10);
    push_back_int(&d, 20);

    tda_deque_pop_front(&d);
    tda_deque_pop_front(&d);

    push_back_int(&d, 30);
    push_back_int(&d, 40);

    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));
    TEST_ASSERT_TRUE(wraps(&d));

    return d;
}

// int32_t deque with room for exactly 'cap', its front at slot 'head', holding
// 100, 101, ... 100+len-1. Walking 'head' over every slot puts the seam everywhere.
[[nodiscard]]
static tda_Deque make_at(size_t cap, size_t head, size_t len) {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, cap, tda_al_default(), &d));

    for (size_t i = 0; i < head; ++i) {
        push_back_int(&d, -1);
    }
    for (size_t i = 0; i < head; ++i) {
        tda_deque_pop_front(&d);
    }
    for (size_t i = 0; i < len; ++i) {
        push_back_int(&d, 100 + (int32_t) i);
    }

    TEST_ASSERT_EQUAL_size_t(cap, tda_deque_cap(&d));

    return d;
}

/* ========== lifetime ========== */

static void test_init_starts_empty_and_unallocated() {
    tda_Deque d = TDA_DEQUE_INIT(int32_t, tda_al_default());

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(&d));
    TEST_ASSERT_EQUAL_size_t(0, tda_deque_cap(&d));
    TEST_ASSERT_EQUAL_size_t(0, tda_deque_bytes(&d));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_deque_elem_layout(&d).size);
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_deque_al(&d)));

    tda_deque_deinit(&d);
}

static void test_init_len_zeroes_its_elems() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_LEN(int32_t, 3, tda_al_default(), &d));

    assert_elems(&d, (int32_t[]){0, 0, 0}, 3);
    TEST_ASSERT_EQUAL_size_t(3 * sizeof(int32_t), tda_deque_bytes(&d));

    tda_deque_deinit(&d);
}

static void test_init_cap_reserves_without_length() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 8, tda_al_default(), &d));

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(&d));
    TEST_ASSERT_EQUAL_size_t(8, tda_deque_cap(&d));

    tda_deque_deinit(&d);
}

static void test_of_keeps_the_order() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, tda_al_default(), &d, 5, 6, 7));

    assert_elems(&d, (int32_t[]){5, 6, 7}, 3);
    TEST_ASSERT_FALSE(wraps(&d));

    tda_deque_deinit(&d);
}

static void test_from_span_copies_the_elems() {
    constexpr int32_t src[4] = {9, 8, 7, 6};

    tda_Deque d = {};
    TDA_TEST_OK(tda_deque_from_span(TDA_SPAN_FROM_DATA(int32_t, src, 4), tda_al_default(), &d));

    assert_elems(&d, (int32_t[]){9, 8, 7, 6}, 4);

    tda_deque_deinit(&d);
}

static void test_deinit_of_an_inert_value_is_noop() {
    tda_Deque x = {};
    tda_deque_deinit(&x);
    tda_deque_deinit(&x);
}

/* ========== ends ========== */

static void test_push_back_appends() {
    tda_Deque d = make_deque(0);

    push_back_int(&d, 1);
    push_back_int(&d, 2);
    push_back_int(&d, 3);

    assert_elems(&d, (int32_t[]){1, 2, 3}, 3);

    tda_deque_deinit(&d);
}

// the same calls from the other end come out reversed
static void test_push_front_prepends() {
    tda_Deque d = make_deque(0);

    push_front_int(&d, 1);
    push_front_int(&d, 2);
    push_front_int(&d, 3);

    assert_elems(&d, (int32_t[]){3, 2, 1}, 3);

    tda_deque_deinit(&d);
}

static void test_pushes_from_both_ends_meet_in_the_middle() {
    tda_Deque d = make_deque(0);

    push_back_int(&d, 0);
    push_front_int(&d, -1);
    push_back_int(&d, 1);
    push_front_int(&d, -2);
    push_back_int(&d, 2);

    assert_elems(&d, (int32_t[]){-2, -1, 0, 1, 2}, 5);

    tda_deque_deinit(&d);
}

static void test_pop_front_and_pop_back_take_from_their_own_ends() {
    tda_Deque d = make_deque(5);

    tda_deque_pop_front(&d);
    tda_deque_pop_back(&d);

    assert_elems(&d, (int32_t[]){1, 2, 3}, 3);

    tda_deque_deinit(&d);
}

static void test_first_and_last_follow_the_ends() {
    tda_Deque d = make_deque(3);

    TEST_ASSERT_EQUAL_INT32(0, *TDA_DEQUE_FRONT_AS(int32_t, &d));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_DEQUE_BACK_AS(int32_t, &d));

    push_front_int(&d, 9);
    push_back_int(&d, 8);

    TEST_ASSERT_EQUAL_INT32(9, *TDA_DEQUE_FRONT_AS(int32_t, &d));
    TEST_ASSERT_EQUAL_INT32(8, *TDA_DEQUE_BACK_AS(int32_t, &d));

    // a single elem is both ends at once
    tda_Deque one = make_deque(1);
    TEST_ASSERT_EQUAL_PTR(tda_deque_front(&one), tda_deque_back(&one));

    tda_deque_deinit(&one);
    tda_deque_deinit(&d);
}

static void test_pop_to_hands_each_end_out() {
    tda_Deque d = make_wrapped(); // 10, 20, 30, 40
    const size_t cap = tda_deque_cap(&d);

    int32_t out = -1;
    tda_deque_pop_front_to(&d, &out);
    TEST_ASSERT_EQUAL_INT32(10, out);

    tda_deque_pop_back_to(&d, &out);
    TEST_ASSERT_EQUAL_INT32(40, out);

    assert_elems(&d, (int32_t[]){20, 30}, 2);
    TEST_ASSERT_EQUAL_size_t(cap, tda_deque_cap(&d));

    tda_deque_deinit(&d);
}

// 'out' may be any slot of the block, the one being dropped included
static void test_pop_to_into_the_deque_itself() {
    tda_Deque d = make_wrapped();

    tda_deque_pop_back_to(&d, tda_deque_get_mut(&d, 1));
    assert_elems(&d, (int32_t[]){10, 40, 30}, 3);

    int32_t *front = tda_deque_front_mut(&d);
    tda_deque_pop_front_to(&d, front);
    TEST_ASSERT_EQUAL_INT32(10, *front);
    assert_elems(&d, (int32_t[]){40, 30}, 2);

    tda_deque_deinit(&d);
}

static void test_pop_to_macros_move_whole_elems() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(Pair, tda_al_default(), &d, {1, 2}, {3, 4}, {5, 6}));

    Pair out = {0, 0};
    TDA_DEQUE_POP_FRONT_TO(Pair, &d, &out);
    TEST_ASSERT_EQUAL_INT64(1, out.a);
    TEST_ASSERT_EQUAL_INT64(2, out.b);

    TDA_DEQUE_POP_BACK_TO(Pair, &d, &out);
    TEST_ASSERT_EQUAL_INT64(5, out.a);
    TEST_ASSERT_EQUAL_INT64(6, out.b);

    TEST_ASSERT_EQUAL_size_t(1, tda_deque_len(&d));

    tda_deque_deinit(&d);
}

/* ========== the ring ========== */

static void test_the_ring_wraps_and_get_stays_relative_to_the_front() {
    tda_Deque d = make_wrapped();

    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);
    TEST_ASSERT_EQUAL_INT32(10, *TDA_DEQUE_FRONT_AS(int32_t, &d));
    TEST_ASSERT_EQUAL_INT32(40, *TDA_DEQUE_BACK_AS(int32_t, &d));

    tda_deque_deinit(&d);
}

// the classic ring bug: growing a split ring must unroll it, not copy the block
static void test_growth_while_wrapped_keeps_the_order() {
    tda_Deque d = make_wrapped();

    push_back_int(&d, 50);

    TEST_ASSERT_TRUE(tda_deque_cap(&d) > 4);
    TEST_ASSERT_FALSE(wraps(&d));
    assert_elems(&d, (int32_t[]){10, 20, 30, 40, 50}, 5);

    tda_deque_deinit(&d);
}

// the same growth from the other end: the new front must not land inside the old run
static void test_growth_while_wrapped_from_the_front_keeps_the_order() {
    tda_Deque d = make_wrapped();

    push_front_int(&d, 5);

    TEST_ASSERT_TRUE(tda_deque_cap(&d) > 4);
    assert_elems(&d, (int32_t[]){5, 10, 20, 30, 40}, 5);

    tda_deque_deinit(&d);
}

static void test_push_front_on_an_empty_deque_allocates() {
    tda_Deque d = make_deque(0);

    push_front_int(&d, 42);

    TEST_ASSERT_EQUAL_size_t(1, tda_deque_len(&d));
    TEST_ASSERT_TRUE(tda_deque_cap(&d) >= 1);
    TEST_ASSERT_EQUAL_INT32(42, *TDA_DEQUE_FRONT_AS(int32_t, &d));

    tda_deque_deinit(&d);
}

// a long alternation drives the head all the way round the buffer several times;
// a plain array kept in step is the oracle
static void test_draining_and_refilling_walks_the_ring_round() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 4, tda_al_default(), &d));

    int32_t want[4] = {0, 0, 0, 0};
    size_t len = 0;

    for (int32_t step = 0; step < 40; ++step) {
        if (len < 4) {
            push_back_int(&d, step);
            want[len++] = step;
        }

        tda_deque_pop_front(&d);
        for (size_t i = 1; i < len; ++i) {
            want[i - 1] = want[i];
        }
        --len;

        push_back_int(&d, step * 10);
        want[len++] = step * 10;

        assert_elems(&d, want, len);
    }

    // the capacity never had to grow: the ring reused the slots it already had
    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));

    tda_deque_deinit(&d);
}

// the value may alias the elem it is set over
static void test_set_from_its_own_elem() {
    tda_Deque d = make_wrapped();

    tda_deque_set(&d, 1, tda_deque_get(&d, 1));
    tda_deque_set(&d, 0, tda_deque_get(&d, 3));

    assert_elems(&d, (int32_t[]){40, 20, 30, 40}, 4);

    tda_deque_deinit(&d);
}

static void test_get_mut_and_set_write_through_to_the_ring() {
    tda_Deque d = make_wrapped();

    *TDA_DEQUE_GET_MUT_AS(int32_t, &d, 0) = -1;
    TDA_DEQUE_SET(int32_t, &d, 3, -4);
    *(int32_t *) tda_deque_front_mut(&d) -= 100;
    *(int32_t *) tda_deque_back_mut(&d) -= 100;

    assert_elems(&d, (int32_t[]){-101, 20, 30, -104}, 4);

    tda_deque_deinit(&d);
}

/* ========== copy ========== */

static void test_copy_is_independent_of_a_wrapped_source() {
    tda_Deque d = make_wrapped();

    tda_Deque copy = {};
    TDA_TEST_OK(tda_deque_copy(&d, &copy));

    // the copy is sized to the content, so it comes out in one run
    TEST_ASSERT_FALSE(wraps(&copy));
    assert_elems(&copy, (int32_t[]){10, 20, 30, 40}, 4);

    TDA_DEQUE_SET(int32_t, &copy, 0, 999);
    TEST_ASSERT_EQUAL_INT32(10, *TDA_DEQUE_GET_AS(int32_t, &d, 0));

    tda_deque_deinit(&copy);
    tda_deque_deinit(&d);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Deque src = make_wrapped();

    tda_Deque dst = {};
    TDA_TEST_OK(tda_deque_copy_with(&src, tda_arena_to_al(&arena), &dst));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_deque_al(&dst)));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_deque_al(&src)));
    TEST_ASSERT_TRUE(tda_deque_eq(&src, &dst));

    // the copy outlives the source, so it owns its elems
    tda_deque_deinit(&src);
    assert_elems(&dst, (int32_t[]){10, 20, 30, 40}, 4);

    tda_deque_deinit(&dst);
    tda_arena_deinit(&arena);
}

// the blocks come from the copy's allocator, not the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));
    tda_test_arena_leave(&arena, 0);

    tda_Deque src = make_deque(4);

    tda_Deque dst;
    TDA_TEST_POISON(dst);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_copy_with(&src, tda_arena_to_al(&arena), &dst));
    TDA_TEST_ASSERT_POISONED(dst);
    TEST_ASSERT_EQUAL_size_t(4, tda_deque_len(&src));

    tda_deque_deinit(&src);
    tda_arena_deinit(&arena);
}

static void test_for_each_walks_a_split_ring_in_order() {
    // the contents wrap, so a walk by block address instead of by index would return the
    // two runs in the wrong order
    tda_Deque d = make_wrapped();
    TEST_ASSERT_TRUE(wraps(&d));

    int32_t seen[4];
    size_t n = 0;
    TDA_DEQUE_FOR_EACH_AS(int32_t, elem, &d) {
        TEST_ASSERT_TRUE(n < 4);
        seen[n++] = *elem;
    }

    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_INT32_ARRAY(((int32_t[]){10, 20, 30, 40}), seen, 4);

    tda_deque_deinit(&d);
}

static void test_for_each_over_an_empty_deque_runs_no_body() {
    tda_Deque d = make_deque(0);

    size_t n = 0;
    TDA_DEQUE_FOR_EACH_AS(int32_t, elem, &d) {
        TDA_UNUSED(elem);
        ++n;
    }

    TEST_ASSERT_EQUAL_size_t(0, n);

    tda_deque_deinit(&d);
}

static void test_for_each_mut_writes_through_every_slot() {
    tda_Deque d = make_wrapped();

    TDA_DEQUE_FOR_EACH_MUT_AS(int32_t, elem, &d) {
        *elem += 1;
    }

    assert_elems(&d, (int32_t[]){11, 21, 31, 41}, 4);

    tda_deque_deinit(&d);
}

static void test_for_each_stops_at_a_break() {
    // the walk is two nested loops, and a bare break would leave only the inner one
    tda_Deque d = make_wrapped();

    size_t n = 0;
    TDA_DEQUE_FOR_EACH_AS(int32_t, elem, &d) {
        ++n;
        if (*elem == 20) {
            break;
        }
    }

    TEST_ASSERT_EQUAL_size_t(2, n);

    n = 0;
    TDA_DEQUE_FOR_EACH_MUT_AS(int32_t, elem, &d) {
        ++n;
        if (*elem == 20) {
            break;
        }
    }

    TEST_ASSERT_EQUAL_size_t(2, n);

    tda_deque_deinit(&d);
}

static void test_for_each_goes_on_past_a_continue() {
    tda_Deque d = make_wrapped();

    int32_t sum = 0;
    TDA_DEQUE_FOR_EACH_AS(int32_t, elem, &d) {
        if (*elem == 20) {
            continue;
        }
        sum += *elem;
    }

    TEST_ASSERT_EQUAL_INT32(10 + 30 + 40, sum);

    tda_deque_deinit(&d);
}

static tda_Deque *counted(tda_Deque *d, size_t *calls) {
    ++*calls;

    return d;
}

static void test_for_each_reads_its_deque_once() {
    tda_Deque d = make_wrapped();

    size_t calls = 0;
    size_t n = 0;
    TDA_DEQUE_FOR_EACH_AS(int32_t, elem, counted(&d, &calls)) {
        TDA_UNUSED(elem);
        ++n;
    }

    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_size_t(1, calls);

    calls = 0;
    TDA_DEQUE_FOR_EACH_MUT_AS(int32_t, elem, counted(&d, &calls)) {
        *elem += 1;
    }

    TEST_ASSERT_EQUAL_size_t(1, calls);
    assert_elems(&d, (int32_t[]){11, 21, 31, 41}, 4);

    tda_deque_deinit(&d);
}

static void test_for_each_nests() {
    // the hidden loop variables are named per line, so the inner walk does not shadow
    // the outer one and a break in it leaves the outer one going
    tda_Deque d = make_wrapped();

    size_t pairs = 0;
    TDA_DEQUE_FOR_EACH_AS(int32_t, outer, &d) {
        TDA_DEQUE_FOR_EACH_AS(int32_t, inner, &d) {
            if (*inner > *outer) {
                break;
            }
            ++pairs;
        }
    }

    TEST_ASSERT_EQUAL_size_t(1 + 2 + 3 + 4, pairs);

    tda_deque_deinit(&d);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque src = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, al, &src, 1, 2, 3));

    tda_Deque dst = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, al, &dst, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    tda_deque_move_assign(&dst, &src);

    // no allocation: the ring is handed over as is
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    assert_elems(&dst, (int32_t[]){1, 2, 3}, 3);
    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(&src));

    tda_deque_deinit(&src);
    tda_deque_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the allocator is part of the value: 'dst' takes the elems with the allocator they came
// from
static void test_move_assign_across_allocators_hands_the_allocator_over() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    // a split ring must arrive in deque order, exactly as in copy
    tda_Deque src = make_wrapped();

    tda_Deque dst = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, tda_arena_to_al(&arena), &dst, 9));

    tda_deque_move_assign(&dst, &src);

    assert_elems(&dst, (int32_t[]){10, 20, 30, 40}, 4);
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_deque_al(&dst)));

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(&src));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_deque_al(&src)));

    tda_deque_deinit(&src);
    tda_deque_deinit(&dst);
    tda_arena_deinit(&arena);
}

// nothing is allocated, so an exhausted allocator on either side cannot stop it
static void test_move_assign_needs_no_memory() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    tda_Deque dst = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, tda_arena_to_al(&arena), &dst, 9));
    tda_test_arena_leave(&arena, 0);

    tda_Deque src = make_deque(4);

    tda_deque_move_assign(&dst, &src);

    assert_elems(&dst, (int32_t[]){0, 1, 2, 3}, 4);
    assert_elems(&src, nullptr, 0);

    tda_deque_deinit(&src);
    tda_deque_deinit(&dst);
    tda_arena_deinit(&arena);
}

// an inert target, `= {}`, takes on everything the source has: layout, allocator and
// all, and the source stays usable
static void test_move_assign_fills_an_inert_target() {
    tda_Deque src = make_deque(4), dst = {};

    tda_deque_move_assign(&dst, &src);

    assert_elems(&dst, (int32_t[]){0, 1, 2, 3}, 4);
    push_back_int(&src, 7);
    assert_elems(&src, (int32_t[]){7}, 1);

    tda_deque_deinit(&src);
    tda_deque_deinit(&dst);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_Deque d = make_deque(3);

    tda_deque_move_assign(&d, &d);

    assert_elems(&d, (int32_t[]){0, 1, 2}, 3);

    tda_deque_deinit(&d);
}

static void test_copy_assign_overwrites_a_longer_target() {
    tda_Deque src = make_deque(2), dst = make_deque(6);

    TDA_TEST_OK(tda_deque_copy_assign(&dst, &src));

    assert_elems(&dst, (int32_t[]){0, 1}, 2);

    tda_deque_deinit(&dst);
    tda_deque_deinit(&src);
}

static void test_copy_assign_grows_a_shorter_target() {
    tda_Deque src = make_wrapped(), dst = make_deque(1);

    TDA_TEST_OK(tda_deque_copy_assign(&dst, &src));

    assert_elems(&dst, (int32_t[]){10, 20, 30, 40}, 4);

    tda_deque_deinit(&dst);
    tda_deque_deinit(&src);
}

static void test_copy_assign_of_itself_changes_nothing() {
    tda_Deque d = make_wrapped();

    TDA_TEST_OK(tda_deque_copy_assign(&d, &d));

    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);

    tda_deque_deinit(&d);
}

// the split ring arrives in the span as one run, in order
static void test_copy_to_span_unwraps_the_contents() {
    tda_Deque d = make_wrapped();

    int32_t buf[4] = {0, 0, 0, 0};
    tda_deque_copy_to_span(&d, TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4));

    TEST_ASSERT_EQUAL_INT32_ARRAY(((int32_t[]){10, 20, 30, 40}), buf, 4);

    tda_deque_deinit(&d);
}

static void test_copy_from_span_writes_back_in_ring_order() {
    tda_Deque d = make_wrapped();

    constexpr int32_t src[4] = {1, 2, 3, 4};
    tda_deque_copy_from_span(&d, TDA_SPAN_FROM_DATA(int32_t, src, 4));

    // the ring is where it was; only the elems changed
    TEST_ASSERT_TRUE(wraps(&d));
    assert_elems(&d, (int32_t[]){1, 2, 3, 4}, 4);

    tda_deque_deinit(&d);
}

static void test_copy_from_span_of_nothing_into_an_empty_deque() {
    tda_Deque d = make_deque(0);

    tda_deque_copy_from_span(&d, TDA_SPAN_FROM_DATA(int32_t, nullptr, 0));

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(&d));

    tda_deque_deinit(&d);
}

static void test_the_span_pair_round_trips_an_untouched_deque() {
    tda_Deque d = make_wrapped();

    int32_t buf[4];
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);

    tda_deque_copy_to_span(&d, s);
    tda_deque_copy_from_span(&d, tda_span_mut_to_span(s));

    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);

    tda_deque_deinit(&d);
}

// hand the contents to algo and take the result back
static void test_the_span_pair_carries_the_deque_through_algo() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 4, tda_al_default(), &d));

    push_back_int(&d, 1);
    push_back_int(&d, 2);
    tda_deque_pop_front(&d);
    push_back_int(&d, 5);
    push_back_int(&d, 3);
    push_back_int(&d, 4);
    TEST_ASSERT_TRUE(wraps(&d));

    int32_t buf[4];
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, tda_deque_len(&d));

    tda_deque_copy_to_span(&d, s);
    tda_span_sort(s, tda_cmp_i32, nullptr);
    tda_deque_copy_from_span(&d, tda_span_mut_to_span(s));

    assert_elems(&d, (int32_t[]){2, 3, 4, 5}, 4);

    tda_deque_deinit(&d);
}

/* ========== to spans ========== */

// every head and length on a ring of six: the two runs, front then back, give the ring
// order, and the front run holds the front elem
static void test_to_spans_are_the_ring_order_split_at_the_seam() {
    constexpr size_t CAP = 6;

    for (size_t head = 0; head < CAP; ++head) {
        for (size_t len = 0; len <= CAP; ++len) {
            tda_Deque d = make_at(CAP, head, len);

            tda_Span front, back;
            tda_deque_to_spans(&d, &front, &back);

            TEST_ASSERT_EQUAL_size_t(len, front.len + back.len);
            TEST_ASSERT_EQUAL_size_t(len > 0, front.len > 0);
            TEST_ASSERT_EQUAL(len > 0 && wraps(&d), back.len > 0);
            for (size_t i = 0; i < len; ++i) {
                const tda_Span run = i < front.len ? front : back;
                const size_t at = i < front.len ? i : i - front.len;
                TEST_ASSERT_EQUAL_PTR(tda_deque_get(&d, i), tda_span_get(run, at));
            }

            tda_deque_deinit(&d);
        }
    }
}

static void test_to_spans_of_a_fresh_deque_are_empty() {
    tda_Deque d = TDA_DEQUE_INIT(int32_t, tda_al_default());

    tda_Span front, back;
    tda_deque_to_spans(&d, &front, &back);

    TEST_ASSERT_EQUAL_size_t(0, front.len);
    TEST_ASSERT_EQUAL_size_t(0, back.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), front.elem.size);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), back.elem.size);

    tda_deque_deinit(&d);
}

// the mutable pair lets an algo work run by run, in place
static void test_to_spans_mut_write_through_to_the_ring() {
    tda_Deque d = make_wrapped();
    TEST_ASSERT_TRUE(wraps(&d));

    tda_SpanMut front, back;
    tda_deque_to_spans_mut(&d, &front, &back);
    TEST_ASSERT_TRUE(back.len > 0);

    TDA_SPAN_FOR_EACH_MUT_AS(int32_t, elem, front) {
        *elem += 1;
    }
    TDA_SPAN_FOR_EACH_MUT_AS(int32_t, elem, back) {
        *elem += 1;
    }

    constexpr int32_t want[4] = {11, 21, 31, 41};
    assert_elems(&d, want, 4);

    tda_deque_deinit(&d);
}

/* ========== insert / remove ========== */

static void test_insert_at_the_front_matches_push_front() {
    tda_Deque d = make_deque(3);

    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &d, 0, 9));

    assert_elems(&d, (int32_t[]){9, 0, 1, 2}, 4);

    tda_deque_deinit(&d);
}

static void test_insert_at_len_matches_push_back() {
    tda_Deque d = make_deque(3);

    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &d, 3, 9));

    assert_elems(&d, (int32_t[]){0, 1, 2, 9}, 4);

    tda_deque_deinit(&d);
}

static void test_insert_into_an_empty_deque() {
    tda_Deque d = make_deque(0);

    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &d, 0, 7));

    assert_elems(&d, (int32_t[]){7}, 1);

    tda_deque_deinit(&d);
}

// the two branches shift opposite sides, so both halves need their own case
static void test_insert_in_the_middle_shifts_either_side() {
    tda_Deque front_half = make_deque(6);
    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &front_half, 2, 99));
    assert_elems(&front_half, (int32_t[]){0, 1, 99, 2, 3, 4, 5}, 7);

    tda_Deque back_half = make_deque(6);
    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &back_half, 4, 99));
    assert_elems(&back_half, (int32_t[]){0, 1, 2, 3, 99, 4, 5}, 7);

    tda_deque_deinit(&back_half);
    tda_deque_deinit(&front_half);
}

// a shift across the seam cannot be one memmove, so the wrapped case gets its own test
static void test_insert_into_a_wrapped_ring() {
    tda_Deque d = make_wrapped();
    tda_deque_pop_back(&d); // room for one, still split

    TEST_ASSERT_TRUE(wraps(&d));
    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &d, 1, 15));

    assert_elems(&d, (int32_t[]){10, 15, 20, 30}, 4);
    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));

    tda_deque_deinit(&d);
}

// an elem of its own from every index into every place, with the seam at every slot,
// once full and once with room: either side may shift, and growing frees the old block
static void test_insert_takes_an_elem_of_its_own_from_anywhere() {
    constexpr size_t cap = 5;

    for (size_t len = cap - 1; len <= cap; ++len) {
        for (size_t head = 0; head < cap; ++head) {
            for (size_t idx = 0; idx <= len; ++idx) {
                for (size_t from = 0; from < len; ++from) {
                    tda_Deque d = make_at(cap, head, len);

                    TDA_TEST_OK(tda_deque_insert(&d, idx, tda_deque_get(&d, from)));

                    int32_t want[cap + 1];
                    for (size_t i = 0, j = 0; i <= len; ++i) {
                        want[i] = 100 + (int32_t) (i == idx ? from : j++);
                    }
                    assert_elems(&d, want, len + 1);

                    tda_deque_deinit(&d);
                }
            }
        }
    }
}

// both pushes onto a full ring with the seam at every slot, so each one grows
static void test_pushes_take_an_elem_of_their_own_while_growing() {
    constexpr size_t cap = 4;

    for (size_t head = 0; head < cap; ++head) {
        for (size_t from = 0; from < cap; ++from) {
            tda_Deque front = make_at(cap, head, cap);
            tda_Deque back = make_at(cap, head, cap);

            TDA_TEST_OK(tda_deque_push_front(&front, tda_deque_get(&front, from)));
            TDA_TEST_OK(tda_deque_push_back(&back, tda_deque_get(&back, from)));

            TEST_ASSERT_EQUAL_INT32(100 + (int32_t) from, *TDA_DEQUE_FRONT_AS(int32_t, &front));
            TEST_ASSERT_EQUAL_INT32(100 + (int32_t) from, *TDA_DEQUE_BACK_AS(int32_t, &back));
            TEST_ASSERT_EQUAL_size_t(cap + 1, tda_deque_len(&front));
            TEST_ASSERT_EQUAL_size_t(cap + 1, tda_deque_len(&back));

            tda_deque_deinit(&front);
            tda_deque_deinit(&back);
        }
    }
}

static void test_insert_moves_whole_elems() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(Pair, tda_al_default(), &d, {1, 10}, {2, 20}, {3, 30}));

    constexpr Pair val = {9, 90};
    TDA_TEST_OK(tda_deque_insert(&d, 1, &val));

    constexpr Pair want[4] = {{1, 10}, {9, 90}, {2, 20}, {3, 30}};
    for (size_t i = 0; i < 4; ++i) {
        const Pair *got = TDA_DEQUE_GET_AS(Pair, &d, i);
        TEST_ASSERT_EQUAL_INT64(want[i].a, got->a);
        TEST_ASSERT_EQUAL_INT64(want[i].b, got->b);
    }

    tda_deque_deinit(&d);
}

static void test_remove_at_the_ends_matches_the_pops() {
    tda_Deque front = make_deque(4);
    tda_deque_remove(&front, 0);
    assert_elems(&front, (int32_t[]){1, 2, 3}, 3);

    tda_Deque back = make_deque(4);
    tda_deque_remove(&back, 3);
    assert_elems(&back, (int32_t[]){0, 1, 2}, 3);

    tda_deque_deinit(&back);
    tda_deque_deinit(&front);
}

static void test_remove_in_the_middle_closes_the_gap_from_either_side() {
    tda_Deque front_half = make_deque(6);
    tda_deque_remove(&front_half, 1);
    assert_elems(&front_half, (int32_t[]){0, 2, 3, 4, 5}, 5);

    tda_Deque back_half = make_deque(6);
    tda_deque_remove(&back_half, 4);
    assert_elems(&back_half, (int32_t[]){0, 1, 2, 3, 5}, 5);

    tda_deque_deinit(&back_half);
    tda_deque_deinit(&front_half);
}

static void test_remove_from_a_wrapped_ring() {
    tda_Deque d = make_wrapped();

    tda_deque_remove(&d, 2);

    assert_elems(&d, (int32_t[]){10, 20, 40}, 3);

    tda_deque_deinit(&d);
}

static void test_insert_then_remove_restores_the_deque() {
    tda_Deque d = make_wrapped();

    for (size_t idx = 0; idx <= 4; ++idx) {
        TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &d, idx, 77));
        TEST_ASSERT_EQUAL_INT32(77, *TDA_DEQUE_GET_AS(int32_t, &d, idx));

        tda_deque_remove(&d, idx);
        assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);
    }

    tda_deque_deinit(&d);
}

// The header promises that the SHORTER side moves; the elems on the other side keep
// their addresses. Room is reserved up front so no growth relocates everything.
static void test_insert_shifts_the_shorter_side() {
    tda_Deque front_half = make_deque(8);
    TDA_TEST_OK(tda_deque_reserve(&front_half, 16));
    const void *back_elem = tda_deque_back(&front_half);

    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &front_half, 2, 99));
    TEST_ASSERT_EQUAL_PTR(back_elem, tda_deque_back(&front_half));

    tda_Deque back_half = make_deque(8);
    TDA_TEST_OK(tda_deque_reserve(&back_half, 16));
    const void *front_elem = tda_deque_front(&back_half);

    TDA_TEST_OK(TDA_DEQUE_INSERT(int32_t, &back_half, 6, 99));
    TEST_ASSERT_EQUAL_PTR(front_elem, tda_deque_front(&back_half));

    tda_deque_deinit(&back_half);
    tda_deque_deinit(&front_half);
}

static void test_remove_shifts_the_shorter_side() {
    tda_Deque front_half = make_deque(8);
    const void *back_elem = tda_deque_back(&front_half);

    tda_deque_remove(&front_half, 1);
    TEST_ASSERT_EQUAL_PTR(back_elem, tda_deque_back(&front_half));

    tda_Deque back_half = make_deque(8);
    const void *front_elem = tda_deque_front(&back_half);

    tda_deque_remove(&back_half, 6);
    TEST_ASSERT_EQUAL_PTR(front_elem, tda_deque_front(&back_half));

    tda_deque_deinit(&back_half);
    tda_deque_deinit(&front_half);
}

/* ========== bulk mods ========== */

static void test_extend_appends_in_order() {
    tda_Deque d = make_wrapped();

    TDA_TEST_OK(TDA_DEQUE_EXTEND(int32_t, &d, 50, 60, 70));

    assert_elems(&d, (int32_t[]){10, 20, 30, 40, 50, 60, 70}, 7);

    tda_deque_deinit(&d);
}

static void test_extend_of_nothing_changes_nothing() {
    tda_Deque d = make_wrapped();

    TDA_TEST_OK(tda_deque_extend(&d, TDA_SPAN_FROM_DATA(int32_t, (int32_t[]){0}, 0)));

    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);
    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));

    tda_deque_deinit(&d);
}

// one-elem extends grow by the same factor as push, not by one slot each
static void test_a_run_of_extends_stays_amortized() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque d = TDA_DEQUE_INIT(int32_t, al);

    for (int32_t i = 0; i < 64; ++i) {
        TDA_TEST_OK(TDA_DEQUE_EXTEND(int32_t, &d, i));
    }

    TEST_ASSERT_EQUAL_size_t(64, tda_deque_len(&d));
    TEST_ASSERT_TRUE(tda_test_probe_requests(&probe) < 20);

    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// every head, length, index and run size of a ring of 8, with and without growth,
// against the same insert on a plain array
static void test_insert_span_matches_a_plain_array_everywhere() {
    constexpr size_t cap = 8;
    constexpr int32_t run[3] = {-1, -2, -3};

    for (size_t head = 0; head < cap; ++head) {
        for (size_t len = 0; len <= cap; ++len) {
            for (size_t idx = 0; idx <= len; ++idx) {
                for (size_t count = 0; count <= 3; ++count) {
                    tda_Deque d = make_at(cap, head, len);

                    TDA_TEST_OK(tda_deque_insert_span(&d, idx, TDA_SPAN_FROM_DATA(int32_t, run, count)));

                    int32_t want[cap + 3];
                    size_t n = 0;
                    for (size_t i = 0; i < idx; ++i) {
                        want[n++] = 100 + (int32_t) i;
                    }
                    for (size_t i = 0; i < count; ++i) {
                        want[n++] = run[i];
                    }
                    for (size_t i = idx; i < len; ++i) {
                        want[n++] = 100 + (int32_t) i;
                    }
                    assert_elems(&d, want, n);

                    tda_deque_deinit(&d);
                }
            }
        }
    }
}

// as with insert: the elems on the side that stays keep their addresses
static void test_insert_span_shifts_the_shorter_side() {
    tda_Deque front_half = make_at(16, 12, 8);
    const void *back_elem = tda_deque_back(&front_half);

    TDA_TEST_OK(TDA_DEQUE_INSERT_SPAN(int32_t, &front_half, 2, 7, 8, 9));
    TEST_ASSERT_EQUAL_PTR(back_elem, tda_deque_back(&front_half));

    tda_Deque back_half = make_at(16, 12, 8);
    const void *front_elem = tda_deque_front(&back_half);

    TDA_TEST_OK(TDA_DEQUE_INSERT_SPAN(int32_t, &back_half, 6, 7, 8, 9));
    TEST_ASSERT_EQUAL_PTR(front_elem, tda_deque_front(&back_half));

    tda_deque_deinit(&back_half);
    tda_deque_deinit(&front_half);
}

static void test_insert_span_reports_a_refused_growth() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 2, al, &d));
    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t, &d, 1));
    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t, &d, 2));

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_DEQUE_INSERT_SPAN(int32_t, &d, 1, 7, 8));

    assert_elems(&d, (int32_t[]){1, 2}, 2);
    TEST_ASSERT_EQUAL_size_t(2, tda_deque_cap(&d));

    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_insert_span_moves_whole_elems() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(Pair, tda_al_default(), &d, {1, 10}, {4, 40}));

    TDA_TEST_OK(TDA_DEQUE_INSERT_SPAN(Pair, &d, 1, {2, 20}, {3, 30}));

    TEST_ASSERT_EQUAL_size_t(4, tda_deque_len(&d));
    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT64((int64_t) i + 1, TDA_DEQUE_GET_AS(Pair, &d, i)->a);
        TEST_ASSERT_EQUAL_INT64(10 * ((int64_t) i + 1), TDA_DEQUE_GET_AS(Pair, &d, i)->b);
    }

    tda_deque_deinit(&d);
}

// every head, length, index and count of a ring of 8 against a plain array
static void test_remove_range_matches_a_plain_array_everywhere() {
    constexpr size_t cap = 8;

    for (size_t head = 0; head < cap; ++head) {
        for (size_t len = 0; len <= cap; ++len) {
            for (size_t idx = 0; idx <= len; ++idx) {
                for (size_t count = 0; count <= len - idx; ++count) {
                    tda_Deque d = make_at(cap, head, len);

                    tda_deque_remove_range(&d, idx, count);

                    int32_t want[cap];
                    size_t n = 0;
                    for (size_t i = 0; i < len; ++i) {
                        if (i < idx || i >= idx + count) {
                            want[n++] = 100 + (int32_t) i;
                        }
                    }
                    assert_elems(&d, want, n);
                    TEST_ASSERT_EQUAL_size_t(cap, tda_deque_cap(&d));

                    tda_deque_deinit(&d);
                }
            }
        }
    }
}

static void test_remove_range_shifts_the_shorter_side() {
    tda_Deque front_half = make_at(16, 12, 8);
    const void *back_elem = tda_deque_back(&front_half);

    tda_deque_remove_range(&front_half, 1, 2);
    TEST_ASSERT_EQUAL_PTR(back_elem, tda_deque_back(&front_half));

    tda_Deque back_half = make_at(16, 12, 8);
    const void *front_elem = tda_deque_front(&back_half);

    tda_deque_remove_range(&back_half, 5, 2);
    TEST_ASSERT_EQUAL_PTR(front_elem, tda_deque_front(&back_half));

    tda_deque_deinit(&back_half);
    tda_deque_deinit(&front_half);
}

// pushes each elem it sees onto the deque at 'ctx' and keeps them all
static bool record(const void *elem, void *ctx) {
    TDA_TEST_OK(tda_deque_push_back(ctx, elem));

    return true;
}

// the ring is split at every slot, so kept elems move across the seam
static void test_retain_keeps_what_passes_in_order() {
    constexpr size_t cap = 8;

    for (size_t head = 0; head < cap; ++head) {
        tda_Deque d = make_at(cap, head, cap); // 100 .. 107

        tda_deque_retain(&d, tda_test_i32_is_even, nullptr);

        assert_elems(&d, (int32_t[]){100, 102, 104, 106}, 4);
        TEST_ASSERT_EQUAL_size_t(cap, tda_deque_cap(&d));

        tda_deque_deinit(&d);
    }
}

// pred sees every elem once, front to back
static void test_retain_asks_once_per_elem_front_to_back() {
    tda_Deque d = make_wrapped();
    tda_Deque seen = make_deque(0);

    tda_deque_retain(&d, record, &seen);

    assert_elems(&seen, (int32_t[]){10, 20, 30, 40}, 4);
    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);

    tda_deque_deinit(&seen);
    tda_deque_deinit(&d);
}

static void test_retain_of_none_empties_the_deque() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, tda_al_default(), &d, 1, 3, 5));

    tda_deque_retain(&d, tda_test_i32_is_even, nullptr);

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(&d));

    tda_deque_deinit(&d);
}

static void test_retain_on_an_empty_deque_asks_nothing() {
    tda_Deque d = make_deque(0);
    tda_Deque seen = make_deque(0);

    tda_deque_retain(&d, record, &seen);

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(&seen));

    tda_deque_deinit(&seen);
    tda_deque_deinit(&d);
}

/* ========== mods ========== */

static void test_clear_empties_but_keeps_the_capacity() {
    tda_Deque d = make_wrapped();
    const size_t cap = tda_deque_cap(&d);

    tda_deque_clear(&d);

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(&d));
    TEST_ASSERT_EQUAL_size_t(cap, tda_deque_cap(&d));

    // and it is usable again from either end
    push_back_int(&d, 1);
    push_front_int(&d, 0);
    assert_elems(&d, (int32_t[]){0, 1}, 2);

    tda_deque_deinit(&d);
}

static void test_reserve_grows_and_never_shrinks() {
    tda_Deque d = make_deque(2);

    TDA_TEST_OK(tda_deque_reserve(&d, 16));
    TEST_ASSERT_EQUAL_size_t(16, tda_deque_cap(&d));

    TDA_TEST_OK(tda_deque_reserve(&d, 4));
    TEST_ASSERT_EQUAL_size_t(16, tda_deque_cap(&d));

    assert_elems(&d, (int32_t[]){0, 1}, 2);

    tda_deque_deinit(&d);
}

// reserve moves the contents into a fresh block, so the seam disappears
static void test_reserve_unwraps_the_ring() {
    tda_Deque d = make_wrapped();

    TDA_TEST_OK(tda_deque_reserve(&d, 32));

    TEST_ASSERT_FALSE(wraps(&d));
    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);

    tda_deque_deinit(&d);
}

static void test_shrink_to_fit_trims_to_len() {
    tda_Deque d = make_wrapped();
    tda_deque_pop_back(&d);

    TDA_TEST_OK(tda_deque_shrink_to_fit(&d));

    TEST_ASSERT_EQUAL_size_t(3, tda_deque_cap(&d));
    TEST_ASSERT_FALSE(wraps(&d));
    assert_elems(&d, (int32_t[]){10, 20, 30}, 3);

    tda_deque_deinit(&d);
}

static void test_shrink_to_fit_of_an_empty_deque_frees_the_block() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 8, al, &d));

    TDA_TEST_OK(tda_deque_shrink_to_fit(&d));

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_cap(&d));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a full ring is already as small as it can be: no new block, the seam stays where it is
static void test_shrink_to_fit_of_a_full_deque_changes_nothing() {
    tda_Deque d = make_wrapped();
    const void *front = tda_deque_front(&d);

    TDA_TEST_OK(tda_deque_shrink_to_fit(&d));

    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));
    TEST_ASSERT_EQUAL_PTR(front, tda_deque_front(&d));
    TEST_ASSERT_TRUE(wraps(&d));
    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);

    tda_deque_deinit(&d);
}

static void test_resize_grows_at_the_back_with_zeros() {
    tda_Deque d = make_wrapped();

    TDA_TEST_OK(tda_deque_resize(&d, 6));

    assert_elems(&d, (int32_t[]){10, 20, 30, 40, 0, 0}, 6);

    tda_deque_deinit(&d);
}

// the new tail may straddle the seam, so growing within the capacity is a separate path
// from growing past it
static void test_resize_grows_inside_a_wrapped_capacity() {
    tda_Deque d = make_wrapped();
    tda_deque_pop_back(&d);
    tda_deque_pop_back(&d);

    TDA_TEST_OK(tda_deque_resize(&d, 4));

    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));
    assert_elems(&d, (int32_t[]){10, 20, 0, 0}, 4);

    tda_deque_deinit(&d);
}

static void test_resize_shrinks_from_the_back() {
    tda_Deque d = make_wrapped();

    TDA_TEST_OK(tda_deque_resize(&d, 2));

    assert_elems(&d, (int32_t[]){10, 20}, 2);

    tda_deque_deinit(&d);
}

// resize(len + 1) grows by one elem to fill in place, so a run of them must cost what
// pushes cost, not one allocation per call
static void test_truncate_drops_from_the_back() {
    tda_Deque d = make_wrapped();

    tda_deque_truncate(&d, 3);
    assert_elems(&d, (int32_t[]){10, 20, 30}, 3);

    tda_deque_truncate(&d, 1);
    assert_elems(&d, (int32_t[]){10}, 1);
    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));

    tda_deque_deinit(&d);
}

// unlike resize, a len past the end is not a request to grow
static void test_truncate_at_or_above_the_len_changes_nothing() {
    tda_Deque d = make_wrapped();

    tda_deque_truncate(&d, 4);
    tda_deque_truncate(&d, 10);

    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);
    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));

    tda_deque_deinit(&d);
}

static void test_a_run_of_resizes_stays_amortized() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque d = TDA_DEQUE_INIT(int32_t, al);

    for (size_t len = 1; len <= 64; ++len) {
        TDA_TEST_OK(tda_deque_resize(&d, len));
    }

    TEST_ASSERT_EQUAL_size_t(64, tda_deque_len(&d));
    TEST_ASSERT_TRUE(tda_test_probe_requests(&probe) < 20);

    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_swap_on_one_allocator_hands_over_the_buffers() {
    tda_Deque a = make_deque(2), b = make_wrapped();

    const size_t a_cap = tda_deque_cap(&a), b_cap = tda_deque_cap(&b);

    tda_deque_swap(&a, &b);

    assert_elems(&a, (int32_t[]){10, 20, 30, 40}, 4);
    assert_elems(&b, (int32_t[]){0, 1}, 2);
    TEST_ASSERT_EQUAL_size_t(b_cap, tda_deque_cap(&a));
    TEST_ASSERT_EQUAL_size_t(a_cap, tda_deque_cap(&b));

    tda_deque_deinit(&b);
    tda_deque_deinit(&a);
}

static void test_swap_of_itself_changes_nothing() {
    tda_Deque d = make_wrapped();

    tda_deque_swap(&d, &d);

    assert_elems(&d, (int32_t[]){10, 20, 30, 40}, 4);

    tda_deque_deinit(&d);
}

static void test_swap_elems_across_the_seam() {
    tda_Deque d = make_wrapped();

    tda_deque_swap_elems(&d, 0, 3);
    assert_elems(&d, (int32_t[]){40, 20, 30, 10}, 4);

    // swapping an elem with itself changes nothing
    tda_deque_swap_elems(&d, 2, 2);
    assert_elems(&d, (int32_t[]){40, 20, 30, 10}, 4);

    tda_deque_deinit(&d);
}

/* ========== failure ========== */

// the new block is taken before the old one goes, so a refusal leaves the target whole
static void test_copy_assign_that_cannot_grow_leaves_the_target() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque dst = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, al, &dst, 7, 8));
    tda_Deque src = make_wrapped();

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_copy_assign(&dst, &src));

    assert_elems(&dst, (int32_t[]){7, 8}, 2);

    tda_deque_deinit(&src);
    tda_deque_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the buffer is refused: nothing may leak
static void test_init_cap_reports_a_refused_buffer() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);
    tda_test_probe_fail_after_next(&probe, 0);

    tda_Deque d = {};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_DEQUE_INIT_CAP(int32_t, 4, al, &d));

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_the_pushes_report_a_refused_growth() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque back = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 1, al, &back));
    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t, &back, 1));

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_DEQUE_PUSH_BACK(int32_t, &back, 2));
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_DEQUE_PUSH_FRONT(int32_t, &back, 0));

    // the deque is unchanged
    assert_elems(&back, (int32_t[]){1}, 1);
    TEST_ASSERT_EQUAL_size_t(1, tda_deque_cap(&back));

    tda_deque_deinit(&back);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_insert_reports_a_refused_growth() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 2, al, &d));
    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t, &d, 1));
    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t, &d, 2));

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_DEQUE_INSERT(int32_t, &d, 1, 99));

    assert_elems(&d, (int32_t[]){1, 2}, 2);

    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_reserve_reports_a_refused_growth() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque d = TDA_DEQUE_INIT(int32_t, al);

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_reserve(&d, 8));

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_cap(&d));

    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// while the capacity holds, neither end asks the allocator for anything
static void test_the_ends_do_not_allocate_while_the_capacity_holds() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 4, al, &d));

    const size_t before = tda_test_probe_requests(&probe);

    for (int32_t i = 0; i < 100; ++i) {
        TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t, &d, i));
        TDA_TEST_OK(TDA_DEQUE_PUSH_FRONT(int32_t, &d, i));
        tda_deque_pop_front(&d);
        tda_deque_pop_back(&d);
    }

    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_size_t(4, tda_deque_cap(&d));

    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== compare ========== */

static void test_eq_matches_the_same_elems() {
    tda_Deque a = make_deque(4), b = make_deque(4);

    TEST_ASSERT_TRUE(tda_deque_eq(&a, &a));
    TEST_ASSERT_TRUE(tda_deque_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_deque_eq(&b, &a));
    TEST_ASSERT_TRUE(tda_deque_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_deque_deinit(&a);
    tda_deque_deinit(&b);
}

static void test_eq_parts_one_differing_elem() {
    tda_Deque a = make_deque(4), b = make_deque(4);
    TDA_DEQUE_SET(int32_t, &b, 3, 99);

    TEST_ASSERT_FALSE(tda_deque_eq(&a, &b));
    TEST_ASSERT_FALSE(tda_deque_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_deque_deinit(&a);
    tda_deque_deinit(&b);
}

static void test_eq_parts_different_lengths() {
    tda_Deque a = make_deque(4);
    tda_Deque shorter = make_deque(3);

    TEST_ASSERT_FALSE(tda_deque_eq(&a, &shorter));
    TEST_ASSERT_FALSE(tda_deque_eq(&shorter, &a));

    tda_deque_deinit(&a);
    tda_deque_deinit(&shorter);
}

static void test_eq_of_two_empties() {
    tda_Deque a = make_deque(0);
    tda_Deque b = make_deque(0);
    tda_Deque one = make_deque(1);

    TEST_ASSERT_TRUE(tda_deque_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_deque_eq_by(&a, &b, tda_eq_i32, nullptr));
    TEST_ASSERT_FALSE(tda_deque_eq(&a, &one));

    tda_deque_deinit(&a);
    tda_deque_deinit(&one);
    tda_deque_deinit(&b);
}

// the two rings hold the same elems from different slots: contents are compared in ring
// order, not as buffers
static void test_eq_ignores_where_the_ring_starts() {
    constexpr int32_t want[4] = {10, 20, 30, 40};

    tda_Deque straight = {};
    TDA_TEST_OK(TDA_DEQUE_FROM_DATA(int32_t, want, 4, tda_al_default(), &straight));
    tda_Deque wrapped = make_wrapped();

    TEST_ASSERT_TRUE(wraps(&wrapped));
    TEST_ASSERT_FALSE(wraps(&straight));
    TEST_ASSERT_TRUE(tda_deque_eq(&straight, &wrapped));
    TEST_ASSERT_TRUE(tda_deque_eq(&wrapped, &straight));
    TEST_ASSERT_TRUE(tda_deque_eq_by(&wrapped, &straight, tda_eq_i32, nullptr));

    tda_deque_deinit(&straight);
    tda_deque_deinit(&wrapped);
}

// the same elems rotated by one: equal as multisets, unequal as deques
static void test_eq_is_order_sensitive() {
    tda_Deque a = make_deque(4), b = make_deque(4);

    const int32_t front = *TDA_DEQUE_FRONT_AS(int32_t, &b);
    tda_deque_pop_front(&b);
    push_back_int(&b, front);

    TEST_ASSERT_EQUAL_size_t(tda_deque_len(&a), tda_deque_len(&b));
    TEST_ASSERT_FALSE(tda_deque_eq(&a, &b));

    tda_deque_deinit(&a);
    tda_deque_deinit(&b);
}

static void test_eq_by_asks_the_equality() {
    constexpr Pair lhs[2] = {{1, 10}, {2, 20}};
    constexpr Pair rhs[2] = {{1, 70}, {2, 80}};

    tda_Deque a = {}, b = {};
    TDA_TEST_OK(TDA_DEQUE_FROM_DATA(Pair, lhs, 2, tda_al_default(), &a));
    TDA_TEST_OK(TDA_DEQUE_FROM_DATA(Pair, rhs, 2, tda_al_default(), &b));

    TEST_ASSERT_FALSE(tda_deque_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_deque_eq_by(&a, &b, tda_test_pair_eq_a, nullptr));

    tda_deque_deinit(&a);
    tda_deque_deinit(&b);
}

/* ========== print ========== */

// printers write to a stream, so cases read it back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_Deque *d) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_deque_fprint(d, stream, tda_fprint_i32, nullptr);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_elems() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, tda_al_default(), &d, 5, 3, 1));

    assert_prints("[5, 3, 1]", &d);

    tda_deque_deinit(&d);
}

static void test_fprint_of_a_single_elem_has_no_separator() {
    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, tda_al_default(), &d, 7));

    assert_prints("[7]", &d);

    tda_deque_deinit(&d);
}

static void test_fprint_of_an_empty_deque() {
    tda_Deque d = make_deque(0);

    assert_prints("[]", &d);

    tda_deque_deinit(&d);
}

// stdout cannot be captured portably, so this only checks that the stdout variant runs
static void test_print_writes_to_stdout() {
    tda_Deque d = make_deque(3);

    tda_deque_print(&d, tda_fprint_i32, nullptr);

    tda_deque_deinit(&d);
}

// each side keeps the allocator its elems came from, and each is given back to it
static void test_swap_across_allocators_carries_each_allocator() {
    tda_TestProbe pa, pb;
    tda_test_probe_reset(&pa);
    tda_test_probe_reset(&pb);
    tda_Al al_a = tda_test_probe_full(&pa), al_b = tda_test_probe_full(&pb);

    tda_Deque a = {}, b = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, al_a, &a, 1, 2));
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, al_b, &b, 3, 4, 5));

    tda_deque_swap(&a, &b);

    TEST_ASSERT_TRUE(tda_al_eq(al_b, tda_deque_al(&a)));
    TEST_ASSERT_TRUE(tda_al_eq(al_a, tda_deque_al(&b)));
    TEST_ASSERT_EQUAL_size_t(3, tda_deque_len(&a));

    tda_deque_deinit(&a);
    TEST_ASSERT_EQUAL_size_t(0, pb.live);
    tda_deque_deinit(&b);
    TEST_ASSERT_EQUAL_size_t(0, pa.live);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_Deque x = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, tda_al_default(), &x, 1, 2));

    tda_deque_deinit(&x);
    TEST_ASSERT_NULL(x.al_.vt);

    tda_deque_deinit(&x);
}

// a value read from a packed buffer may sit at any address; insert copies it as push
// does, without asking it to be aligned
static void test_insert_takes_an_unaligned_val() {
    alignas(int32_t) unsigned char raw[8] = {};
    constexpr int32_t val = 42;
    memcpy(raw + 1, &val, sizeof(val));

    tda_Deque x = TDA_DEQUE_INIT(int32_t, tda_al_default());
    TDA_TEST_OK(tda_deque_insert(&x, 0, raw + 1));

    TEST_ASSERT_EQUAL_INT32(42, *TDA_DEQUE_GET_AS(int32_t, &x, 0));

    tda_deque_deinit(&x);
}

static constexpr tda_Layout TWO_BYTES = {.size = 2, .align = 2};

// len * elem.size overflows size_t: reported, never attempted
static void test_init_len_and_init_cap_report_size_overflow() {
    tda_Deque d = {};

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_init_len(SIZE_MAX, TWO_BYTES, tda_al_default(), &d));
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_init_cap(SIZE_MAX, TWO_BYTES, tda_al_default(), &d));

    constexpr int32_t src[1] = {1};
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_from_data(src, SIZE_MAX, TWO_BYTES, tda_al_default(), &d));

    TEST_ASSERT_NULL(d.al_.vt);
}

// a growth past size_t is refused, and the deque is left as it was
static void test_the_growths_report_size_overflow_and_change_nothing() {
    tda_Deque d = make_deque(3);
    const size_t cap = tda_deque_cap(&d);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_reserve(&d, SIZE_MAX));
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_resize(&d, SIZE_MAX));

    constexpr int32_t src[1] = {1};
    const tda_Span huge = TDA_SPAN_FROM_DATA(int32_t, src, SIZE_MAX);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_extend(&d, huge));
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_insert_span(&d, 1, huge));

    constexpr int32_t want[3] = {0, 1, 2};
    assert_elems(&d, want, 3);
    TEST_ASSERT_EQUAL_size_t(cap, tda_deque_cap(&d));

    tda_deque_deinit(&d);
}

// a refused shrink keeps the block, and the elems in it
static void test_shrink_to_fit_reports_a_refused_block() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_INIT_CAP(int32_t, 8, al, &d));
    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t, &d, 1));
    TDA_TEST_OK(TDA_DEQUE_PUSH_BACK(int32_t, &d, 2));

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_shrink_to_fit(&d));

    TEST_ASSERT_EQUAL_size_t(8, tda_deque_cap(&d));
    constexpr int32_t want[2] = {1, 2};
    assert_elems(&d, want, 2);

    probe.fail_after = SIZE_MAX;
    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a refused growth leaves the len and the elems
static void test_resize_reports_a_refused_growth() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_bare(&probe);

    tda_Deque d = {};
    TDA_TEST_OK(TDA_DEQUE_OF(int32_t, al, &d, 1, 2));

    tda_test_probe_fail_after_next(&probe, 0);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_deque_resize(&d, 10));

    constexpr int32_t want[2] = {1, 2};
    assert_elems(&d, want, 2);

    probe.fail_after = SIZE_MAX;
    tda_deque_deinit(&d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}


/* ========== print errors ========== */

// the ctx reaches every call, every elem is still asked after a failure, and the first
// failure is what comes back; print adds only its newline
static void test_fprint_hands_on_the_ctx_and_the_first_failure() {
    tda_Deque d = make_deque(3);
    tda_TestPrint pr = {.fail_from = 2};
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    TEST_ASSERT_EQUAL_INT(-2, tda_deque_fprint(&d, stream, tda_test_fprint_i32_failing, &pr));
    TEST_ASSERT_EQUAL_size_t(3, pr.calls);
    fclose(stream);

    pr = (tda_TestPrint){.fail_from = 0};
    TEST_ASSERT_EQUAL_INT(0, tda_deque_print(&d, tda_test_fprint_i32_failing, &pr));
    pr = (tda_TestPrint){.fail_from = 2};
    TEST_ASSERT_EQUAL_INT(-2, tda_deque_print(&d, tda_test_fprint_i32_failing, &pr));

    FILE *ro = tda_test_unwritable_stream();
    TEST_ASSERT_LESS_THAN_INT(0, tda_deque_fprint(&d, ro, tda_fprint_i32, nullptr));
    fclose(ro);

    tda_deque_deinit(&d);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_empty_and_unallocated);
    RUN_TEST(test_init_len_zeroes_its_elems);
    RUN_TEST(test_init_cap_reserves_without_length);
    RUN_TEST(test_of_keeps_the_order);
    RUN_TEST(test_from_span_copies_the_elems);
    RUN_TEST(test_deinit_of_an_inert_value_is_noop);
    RUN_TEST(test_deinit_twice_is_noop);

    RUN_TEST(test_push_back_appends);
    RUN_TEST(test_push_front_prepends);
    RUN_TEST(test_pushes_from_both_ends_meet_in_the_middle);
    RUN_TEST(test_pop_front_and_pop_back_take_from_their_own_ends);
    RUN_TEST(test_first_and_last_follow_the_ends);

    RUN_TEST(test_pop_to_hands_each_end_out);
    RUN_TEST(test_pop_to_into_the_deque_itself);
    RUN_TEST(test_pop_to_macros_move_whole_elems);

    RUN_TEST(test_the_ring_wraps_and_get_stays_relative_to_the_front);
    RUN_TEST(test_growth_while_wrapped_keeps_the_order);
    RUN_TEST(test_growth_while_wrapped_from_the_front_keeps_the_order);
    RUN_TEST(test_push_front_on_an_empty_deque_allocates);
    RUN_TEST(test_draining_and_refilling_walks_the_ring_round);
    RUN_TEST(test_get_mut_and_set_write_through_to_the_ring);
    RUN_TEST(test_set_from_its_own_elem);

    RUN_TEST(test_copy_is_independent_of_a_wrapped_source);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_for_each_walks_a_split_ring_in_order);
    RUN_TEST(test_for_each_over_an_empty_deque_runs_no_body);
    RUN_TEST(test_for_each_mut_writes_through_every_slot);
    RUN_TEST(test_for_each_stops_at_a_break);
    RUN_TEST(test_for_each_goes_on_past_a_continue);
    RUN_TEST(test_for_each_reads_its_deque_once);
    RUN_TEST(test_for_each_nests);

    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_hands_the_allocator_over);
    RUN_TEST(test_move_assign_needs_no_memory);
    RUN_TEST(test_move_assign_fills_an_inert_target);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_assign_overwrites_a_longer_target);
    RUN_TEST(test_copy_assign_grows_a_shorter_target);
    RUN_TEST(test_copy_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_to_span_unwraps_the_contents);
    RUN_TEST(test_copy_from_span_writes_back_in_ring_order);
    RUN_TEST(test_copy_from_span_of_nothing_into_an_empty_deque);
    RUN_TEST(test_the_span_pair_round_trips_an_untouched_deque);
    RUN_TEST(test_the_span_pair_carries_the_deque_through_algo);

    RUN_TEST(test_to_spans_are_the_ring_order_split_at_the_seam);
    RUN_TEST(test_to_spans_of_a_fresh_deque_are_empty);
    RUN_TEST(test_to_spans_mut_write_through_to_the_ring);

    RUN_TEST(test_insert_at_the_front_matches_push_front);
    RUN_TEST(test_insert_at_len_matches_push_back);
    RUN_TEST(test_insert_into_an_empty_deque);
    RUN_TEST(test_insert_in_the_middle_shifts_either_side);
    RUN_TEST(test_insert_into_a_wrapped_ring);
    RUN_TEST(test_insert_takes_an_elem_of_its_own_from_anywhere);
    RUN_TEST(test_pushes_take_an_elem_of_their_own_while_growing);
    RUN_TEST(test_insert_moves_whole_elems);
    RUN_TEST(test_remove_at_the_ends_matches_the_pops);
    RUN_TEST(test_remove_in_the_middle_closes_the_gap_from_either_side);
    RUN_TEST(test_remove_from_a_wrapped_ring);
    RUN_TEST(test_insert_then_remove_restores_the_deque);
    RUN_TEST(test_insert_shifts_the_shorter_side);
    RUN_TEST(test_remove_shifts_the_shorter_side);

    RUN_TEST(test_extend_appends_in_order);
    RUN_TEST(test_extend_of_nothing_changes_nothing);
    RUN_TEST(test_a_run_of_extends_stays_amortized);
    RUN_TEST(test_insert_span_matches_a_plain_array_everywhere);
    RUN_TEST(test_insert_span_shifts_the_shorter_side);
    RUN_TEST(test_insert_span_reports_a_refused_growth);
    RUN_TEST(test_insert_span_moves_whole_elems);
    RUN_TEST(test_remove_range_matches_a_plain_array_everywhere);
    RUN_TEST(test_remove_range_shifts_the_shorter_side);
    RUN_TEST(test_retain_keeps_what_passes_in_order);
    RUN_TEST(test_retain_asks_once_per_elem_front_to_back);
    RUN_TEST(test_retain_of_none_empties_the_deque);
    RUN_TEST(test_retain_on_an_empty_deque_asks_nothing);

    RUN_TEST(test_clear_empties_but_keeps_the_capacity);
    RUN_TEST(test_reserve_grows_and_never_shrinks);
    RUN_TEST(test_reserve_unwraps_the_ring);
    RUN_TEST(test_shrink_to_fit_trims_to_len);
    RUN_TEST(test_shrink_to_fit_of_an_empty_deque_frees_the_block);
    RUN_TEST(test_shrink_to_fit_of_a_full_deque_changes_nothing);
    RUN_TEST(test_resize_grows_at_the_back_with_zeros);
    RUN_TEST(test_resize_grows_inside_a_wrapped_capacity);
    RUN_TEST(test_resize_shrinks_from_the_back);
    RUN_TEST(test_truncate_drops_from_the_back);
    RUN_TEST(test_truncate_at_or_above_the_len_changes_nothing);
    RUN_TEST(test_a_run_of_resizes_stays_amortized);
    RUN_TEST(test_swap_on_one_allocator_hands_over_the_buffers);
    RUN_TEST(test_swap_of_itself_changes_nothing);
    RUN_TEST(test_swap_elems_across_the_seam);
    RUN_TEST(test_init_cap_reports_a_refused_buffer);
    RUN_TEST(test_copy_assign_that_cannot_grow_leaves_the_target);
    RUN_TEST(test_the_pushes_report_a_refused_growth);
    RUN_TEST(test_insert_reports_a_refused_growth);
    RUN_TEST(test_reserve_reports_a_refused_growth);
    RUN_TEST(test_the_ends_do_not_allocate_while_the_capacity_holds);


    RUN_TEST(test_eq_matches_the_same_elems);
    RUN_TEST(test_eq_parts_one_differing_elem);
    RUN_TEST(test_eq_parts_different_lengths);
    RUN_TEST(test_eq_of_two_empties);
    RUN_TEST(test_eq_ignores_where_the_ring_starts);
    RUN_TEST(test_eq_is_order_sensitive);
    RUN_TEST(test_eq_by_asks_the_equality);

    RUN_TEST(test_fprint_writes_the_elems);
    RUN_TEST(test_fprint_of_a_single_elem_has_no_separator);
    RUN_TEST(test_fprint_of_an_empty_deque);
    RUN_TEST(test_print_writes_to_stdout);

    RUN_TEST(test_swap_across_allocators_carries_each_allocator);

    RUN_TEST(test_insert_takes_an_unaligned_val);

    RUN_TEST(test_init_len_and_init_cap_report_size_overflow);
    RUN_TEST(test_the_growths_report_size_overflow_and_change_nothing);
    RUN_TEST(test_shrink_to_fit_reports_a_refused_block);
    RUN_TEST(test_resize_reports_a_refused_growth);

    RUN_TEST(test_fprint_hands_on_the_ctx_and_the_first_failure);

    return UNITY_END();
}
