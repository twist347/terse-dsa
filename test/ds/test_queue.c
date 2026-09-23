#include "tda/ds/queue.h"
#include "tda/algo/search.h"
#include "tda/algo/sort.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
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

/* ========== helpers ========== */

static constexpr int32_t SPREAD[] = {5, 1, 9, 9, 3, 7, 2, 8, 3, 6, 0, 4, 9, 1};
static constexpr size_t SPREAD_LEN = sizeof(SPREAD) / sizeof(SPREAD[0]);

static void push_int(tda_Queue *q, int32_t val) {
    TDA_TEST_OK(tda_queue_push(q, &val));
}

// int32_t queue over the default allocator, filled one push at a time
[[nodiscard]]
static tda_Queue *make_queue(const int32_t *src, size_t n) {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW(int32_t, tda_al_default(), &q));

    for (size_t i = 0; i < n; ++i) {
        push_int(q, src[i]);
    }
    return q;
}

// the same elems handed over in one go instead
[[nodiscard]]
static tda_Queue *make_queue_from(const int32_t *src, size_t n) {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_FROM_DATA(int32_t, src, n, tda_al_default(), &q));

    return q;
}

// {10, 20, 30, 40} in a ring of exactly four slots that is split across the end of the
// buffer. The queue hides the ring, so the proof is the capacity: two pops freed the two
// leading slots and the two pushes that follow fit without growing, which they could only
// do by wrapping around
[[nodiscard]]
static tda_Queue *make_wrapped(void) {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW_CAP(int32_t, 4, tda_al_default(), &q));

    push_int(q, 1);
    push_int(q, 2);
    push_int(q, 10);
    push_int(q, 20);

    tda_queue_pop(q);
    tda_queue_pop(q);

    push_int(q, 30);
    push_int(q, 40);

    TEST_ASSERT_EQUAL_size_t(4, tda_queue_cap(q));
    TEST_ASSERT_EQUAL_size_t(4, tda_queue_len(q));

    return q;
}

// reads the contents without disturbing them — the only way in, since the queue hands
// out no index
static void assert_elems(const tda_Queue *q, const int32_t *want, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, tda_queue_len(q));

    int32_t got[64];
    TEST_ASSERT_TRUE(n <= 64);

    tda_queue_copy_to_span(q, TDA_SPAN_FROM_DATA_MUT(int32_t, got, n));
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, got, n);
}

// empties the queue through front + pop, checking at every step that the oldest elem is
// the one on offer and that exactly one left. Draining is checked here rather than in a
// test of its own because the order must hold after EVERY pop, not just the last one
static void assert_drains(tda_Queue *q, const int32_t *want, size_t n) {
    TEST_ASSERT_EQUAL_size_t(n, tda_queue_len(q));

    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_EQUAL_INT32(want[i], *TDA_QUEUE_FRONT_AS(int32_t, q));
        TEST_ASSERT_EQUAL_INT32(want[n - 1], *TDA_QUEUE_BACK_AS(int32_t, q));
        tda_queue_pop(q);

        TEST_ASSERT_EQUAL_size_t(n - i - 1, tda_queue_len(q));
    }
}

/* ========== lifetime ========== */

static void test_new_starts_empty() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW(int32_t, tda_al_default(), &q));

    TEST_ASSERT_EQUAL_size_t(0, tda_queue_len(q));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_queue_elem_size(q));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_queue_al(q));

    tda_queue_drop(q);
}

static void test_new_cap_reserves_without_length() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW_CAP(int32_t, 16, tda_al_default(), &q));

    TEST_ASSERT_EQUAL_size_t(0, tda_queue_len(q));
    TEST_ASSERT_EQUAL_size_t(16, tda_queue_cap(q));

    tda_queue_drop(q);
}

// the queue takes them in the order they are written, so the first one written is the
// first one served
static void test_from_data_keeps_arrival_order() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);

    assert_elems(q, SPREAD, SPREAD_LEN);
    TEST_ASSERT_EQUAL_INT32(SPREAD[0], *TDA_QUEUE_FRONT_AS(int32_t, q));
    TEST_ASSERT_EQUAL_INT32(SPREAD[SPREAD_LEN - 1], *TDA_QUEUE_BACK_AS(int32_t, q));

    tda_queue_drop(q);
}

static void test_from_data_empty_stays_empty() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_FROM_DATA(int32_t, nullptr, 0, tda_al_default(), &q));

    TEST_ASSERT_EQUAL_size_t(0, tda_queue_len(q));

    tda_queue_drop(q);
}

static void test_from_span_copies_the_view() {
    constexpr int32_t src[4] = {9, 8, 7, 6};

    tda_Queue *q = nullptr;
    TDA_TEST_OK(tda_queue_from_span(TDA_SPAN_FROM_DATA(int32_t, src, 4), tda_al_default(), &q));

    assert_elems(q, src, 4);

    tda_queue_drop(q);
}

// an elem wider than a word must travel whole, not by its first field
static void test_from_data_copies_whole_elems() {
    constexpr Pair src[3] = {{1, 10}, {2, 20}, {3, 30}};

    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_FROM_DATA(Pair, src, 3, tda_al_default(), &q));

    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_queue_elem_size(q));

    Pair got[3];
    tda_queue_copy_to_span(q, TDA_SPAN_FROM_DATA_MUT(Pair, got, 3));
    for (size_t i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_INT64(src[i].a, got[i].a);
        TEST_ASSERT_EQUAL_INT64(src[i].b, got[i].b);
    }

    tda_queue_drop(q);
}

static void test_drop_null_is_noop() {
    tda_queue_drop(nullptr);
}

// three blocks go into a filled queue — the deque, its buffer, and the header that hides
// the deque — and drop must hand back all three. The default allocator would say nothing
// about it, so the count comes from a probe
static void test_drop_hands_back_everything_it_took() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_FROM_DATA(int32_t, SPREAD, SPREAD_LEN, &al, &q));
    TEST_ASSERT_EQUAL_size_t(3, probe.live);

    tda_queue_drop(q);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
    TEST_ASSERT_EQUAL_size_t(3, probe.dealloc_calls);
}

/* ========== fifo ========== */

static void test_pushes_leave_in_the_order_they_arrived() {
    tda_Queue *q = make_queue(SPREAD, SPREAD_LEN);

    assert_drains(q, SPREAD, SPREAD_LEN);

    tda_queue_drop(q);
}

// growth relocates every elem, so the order has to survive a move of the whole ring —
// this queue outgrows its first block several times over
static void test_order_survives_growth() {
    int32_t want[64];
    for (int32_t i = 0; i < 64; ++i) {
        want[i] = i * 3;
    }

    tda_Queue *q = make_queue(want, 64);

    TEST_ASSERT_TRUE(tda_queue_cap(q) >= 64);
    assert_drains(q, want, 64);

    tda_queue_drop(q);
}

// the ring is reused as it empties: pushing and popping in step keeps the queue short
// while far more elems than it holds pass through, so the buffer wraps many times over
static void test_pushing_and_popping_in_step_stays_in_order() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW(int32_t, tda_al_default(), &q));

    push_int(q, 0);
    push_int(q, 1);
    push_int(q, 2);

    for (int32_t i = 3; i < 100; ++i) {
        TEST_ASSERT_EQUAL_INT32(i - 3, *TDA_QUEUE_FRONT_AS(int32_t, q));
        tda_queue_pop(q);
        push_int(q, i);

        TEST_ASSERT_EQUAL_size_t(3, tda_queue_len(q));
        TEST_ASSERT_EQUAL_INT32(i, *TDA_QUEUE_BACK_AS(int32_t, q));
    }

    constexpr int32_t want[3] = {97, 98, 99};
    assert_drains(q, want, 3);

    tda_queue_drop(q);
}

static void test_pushes_after_a_full_drain_are_ordered_again() {
    tda_Queue *q = make_queue(SPREAD, SPREAD_LEN);
    assert_drains(q, SPREAD, SPREAD_LEN);

    constexpr int32_t again[4] = {41, 42, 43, 44};
    for (size_t i = 0; i < 4; ++i) {
        push_int(q, again[i]);
    }
    assert_drains(q, again, 4);

    tda_queue_drop(q);
}

// equal elems are not one elem: every copy pushed must come back
static void test_duplicates_all_come_back() {
    constexpr int32_t src[6] = {7, 7, 7, 7, 7, 7};

    tda_Queue *q = make_queue(src, 6);

    assert_drains(q, src, 6);

    tda_queue_drop(q);
}

static void test_a_wrapped_queue_drains_in_order() {
    tda_Queue *q = make_wrapped();

    constexpr int32_t want[4] = {10, 20, 30, 40};
    assert_drains(q, want, 4);

    tda_queue_drop(q);
}

// the elem that has to move is the one that wrapped, so growing a split ring is where
// the order is easiest to lose
static void test_a_wrapped_queue_keeps_its_order_through_growth() {
    tda_Queue *q = make_wrapped();

    push_int(q, 50);
    TEST_ASSERT_TRUE(tda_queue_cap(q) > 4);

    constexpr int32_t want[5] = {10, 20, 30, 40, 50};
    assert_drains(q, want, 5);

    tda_queue_drop(q);
}

/* ========== access ========== */

static void test_front_reads_without_removing() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);

    TEST_ASSERT_EQUAL_INT32(SPREAD[0], *TDA_QUEUE_FRONT_AS(int32_t, q));
    TEST_ASSERT_EQUAL_INT32(SPREAD[0], *TDA_QUEUE_FRONT_AS(int32_t, q));
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_queue_len(q));

    tda_queue_drop(q);
}

static void test_back_follows_the_last_push() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW(int32_t, tda_al_default(), &q));

    for (int32_t i = 0; i < 8; ++i) {
        push_int(q, i);
        TEST_ASSERT_EQUAL_INT32(i, *TDA_QUEUE_BACK_AS(int32_t, q));
        TEST_ASSERT_EQUAL_INT32(0, *TDA_QUEUE_FRONT_AS(int32_t, q));
    }

    tda_queue_drop(q);
}

// there is no order over the elems to break, so writing through an end is legal —
// the queue only rules where they enter and leave
static void test_the_ends_are_writable() {
    tda_Queue *q = make_wrapped();

    *TDA_QUEUE_FRONT_MUT_AS(int32_t, q) = -1;
    *TDA_QUEUE_BACK_MUT_AS(int32_t, q) = -4;

    constexpr int32_t want[4] = {-1, 20, 30, -4};
    assert_elems(q, want, 4);

    tda_queue_drop(q);
}

// on a queue of one the two ends are the same elem
static void test_front_and_back_meet_on_a_single_elem() {
    tda_Queue *q = make_queue_from(SPREAD, 1);

    TEST_ASSERT_EQUAL_PTR(tda_queue_front(q), tda_queue_back(q));
    TEST_ASSERT_EQUAL_PTR(tda_queue_front(q), tda_queue_front_mut(q));

    tda_queue_drop(q);
}

/* ========== info ========== */

static void test_len_follows_push_and_pop() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW(int32_t, tda_al_default(), &q));

    for (size_t i = 0; i < 5; ++i) {
        TEST_ASSERT_EQUAL_size_t(i, tda_queue_len(q));
        push_int(q, (int32_t) i);
    }
    for (size_t i = 5; i > 0; --i) {
        TEST_ASSERT_EQUAL_size_t(i, tda_queue_len(q));
        tda_queue_pop(q);
    }
    TEST_ASSERT_EQUAL_size_t(0, tda_queue_len(q));

    tda_queue_drop(q);
}

// popping hands nothing back to the allocator: the room stays for the next push
static void test_pop_leaves_the_capacity_alone() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);
    const size_t cap = tda_queue_cap(q);

    tda_queue_pop(q);
    tda_queue_pop(q);

    TEST_ASSERT_EQUAL_size_t(cap, tda_queue_cap(q));

    tda_queue_drop(q);
}

/* ========== mods ========== */

static void test_clear_empties_without_giving_back_the_room() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);
    const size_t cap = tda_queue_cap(q);

    tda_queue_clear(q);

    TEST_ASSERT_EQUAL_size_t(0, tda_queue_len(q));
    TEST_ASSERT_EQUAL_size_t(cap, tda_queue_cap(q));

    tda_queue_drop(q);
}

// clearing a split ring must not leave the front pointing into the middle of it
static void test_clear_leaves_a_usable_queue() {
    tda_Queue *q = make_wrapped();

    tda_queue_clear(q);

    constexpr int32_t again[3] = {1, 2, 3};
    for (size_t i = 0; i < 3; ++i) {
        push_int(q, again[i]);
    }
    assert_drains(q, again, 3);

    tda_queue_drop(q);
}

static void test_reserve_grows_the_room_only() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);

    TDA_TEST_OK(tda_queue_reserve(q, 100));

    TEST_ASSERT_TRUE(tda_queue_cap(q) >= 100);
    assert_elems(q, SPREAD, SPREAD_LEN);

    tda_queue_drop(q);
}

static void test_reserve_below_the_capacity_changes_nothing() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);
    const size_t cap = tda_queue_cap(q);

    TDA_TEST_OK(tda_queue_reserve(q, 1));

    TEST_ASSERT_EQUAL_size_t(cap, tda_queue_cap(q));

    tda_queue_drop(q);
}

static void test_shrink_to_fit_keeps_the_order() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);
    TDA_TEST_OK(tda_queue_reserve(q, 100));

    TDA_TEST_OK(tda_queue_shrink_to_fit(q));

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_queue_cap(q));
    assert_elems(q, SPREAD, SPREAD_LEN);

    tda_queue_drop(q);
}

// shrinking a split ring has to unwrap it on the way into the smaller block
static void test_shrink_to_fit_unwraps_what_it_moves() {
    tda_Queue *q = make_wrapped();
    tda_queue_pop(q);

    TDA_TEST_OK(tda_queue_shrink_to_fit(q));

    TEST_ASSERT_EQUAL_size_t(3, tda_queue_cap(q));
    constexpr int32_t want[3] = {20, 30, 40};
    assert_elems(q, want, 3);

    tda_queue_drop(q);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_Queue *src = make_queue_from(SPREAD, SPREAD_LEN);

    tda_Queue *dst = nullptr;
    TDA_TEST_OK(tda_queue_copy(src, &dst));

    tda_queue_pop(dst);
    push_int(dst, 777);

    assert_elems(src, SPREAD, SPREAD_LEN);
    TEST_ASSERT_EQUAL_INT32(SPREAD[1], *TDA_QUEUE_FRONT_AS(int32_t, dst));
    TEST_ASSERT_EQUAL_INT32(777, *TDA_QUEUE_BACK_AS(int32_t, dst));

    tda_queue_drop(src);
    tda_queue_drop(dst);
}

static void test_copy_inherits_the_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Queue *src = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, arena, &src, 5, 1, 3));

    tda_Queue *dst = nullptr;
    TDA_TEST_OK(tda_queue_copy(src, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_queue_al(dst));
    TEST_ASSERT_EQUAL_INT32(5, *TDA_QUEUE_FRONT_AS(int32_t, dst));

    tda_queue_drop(src);
    tda_queue_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Queue *src = make_wrapped();

    tda_Queue *dst = nullptr;
    TDA_TEST_OK(tda_queue_copy_with(src, arena, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_queue_al(dst));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_queue_al(src));
    TEST_ASSERT_TRUE(tda_queue_eq(src, dst));

    // the header goes to the same allocator as the elems, so the copy outlives the source
    tda_queue_drop(src);
    TEST_ASSERT_EQUAL_INT32(10, *TDA_QUEUE_FRONT_AS(int32_t, dst));

    tda_queue_drop(dst);
    tda_al_arena_drop(arena);
}

// the blocks are asked of the allocator the copy is going to, not of the source's
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    tda_Queue *src = make_queue_from(SPREAD, SPREAD_LEN);

    tda_Queue *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_queue_copy_with(src, arena, &dst));
    TEST_ASSERT_NULL(dst);
    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_queue_len(src));

    tda_queue_drop(src);
    tda_al_arena_drop(arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Queue *src = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, &al, &src, 1, 2, 3));

    tda_Queue *dst = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, &al, &dst, 9));

    const size_t requests = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_queue_move_assign(dst, src));

    // nothing was asked of the allocator: the deque's ring changed hands
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(3, tda_queue_len(dst));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_QUEUE_FRONT_AS(int32_t, dst));
    TEST_ASSERT_EQUAL_size_t(0, tda_queue_len(src));

    tda_queue_drop(src);
    tda_queue_drop(dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_move_assign_across_allocators_empties_the_source() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    // a split ring must arrive in queue order, exactly as in copy
    tda_Queue *src = make_wrapped();

    tda_Queue *dst = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, arena, &dst, 9));

    TDA_TEST_OK(tda_queue_move_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(4, tda_queue_len(dst));
    TEST_ASSERT_EQUAL_INT32(10, *TDA_QUEUE_FRONT_AS(int32_t, dst));
    TEST_ASSERT_EQUAL_PTR(arena, tda_queue_al(dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_queue_len(src));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_queue_al(src));

    tda_queue_drop(src);
    tda_queue_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_across_allocators_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Queue *dst = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, arena, &dst, 9));
    tda_test_arena_leave(arena, 0);

    tda_Queue *src = make_queue_from(SPREAD, SPREAD_LEN);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_queue_move_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_queue_len(src));
    TEST_ASSERT_EQUAL_size_t(1, tda_queue_len(dst));
    TEST_ASSERT_EQUAL_INT32(9, *TDA_QUEUE_FRONT_AS(int32_t, dst));

    tda_queue_drop(src);
    tda_queue_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);

    TDA_TEST_OK(tda_queue_move_assign(q, q));

    TEST_ASSERT_EQUAL_size_t(SPREAD_LEN, tda_queue_len(q));
    TEST_ASSERT_EQUAL_INT32(SPREAD[0], *TDA_QUEUE_FRONT_AS(int32_t, q));

    tda_queue_drop(q);
}

// the copy of a split ring must come out in queue order, not in buffer order
static void test_copy_of_a_wrapped_queue_keeps_the_order() {
    tda_Queue *src = make_wrapped();

    tda_Queue *dst = nullptr;
    TDA_TEST_OK(tda_queue_copy(src, &dst));

    constexpr int32_t want[4] = {10, 20, 30, 40};
    assert_drains(dst, want, 4);

    tda_queue_drop(src);
    tda_queue_drop(dst);
}

static void test_copy_of_empty_stays_empty() {
    tda_Queue *src = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW(int32_t, tda_al_default(), &src));

    tda_Queue *dst = nullptr;
    TDA_TEST_OK(tda_queue_copy(src, &dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_queue_len(dst));

    tda_queue_drop(src);
    tda_queue_drop(dst);
}

static void test_copy_assign_overwrites_the_target() {
    tda_Queue *src = make_queue_from(SPREAD, SPREAD_LEN);
    tda_Queue *dst = make_wrapped();

    TDA_TEST_OK(tda_queue_copy_assign(dst, src));

    assert_elems(dst, SPREAD, SPREAD_LEN);
    assert_elems(src, SPREAD, SPREAD_LEN);

    tda_queue_drop(src);
    tda_queue_drop(dst);
}

static void test_copy_assign_self_is_noop() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);

    TDA_TEST_OK(tda_queue_copy_assign(q, q));

    assert_elems(q, SPREAD, SPREAD_LEN);

    tda_queue_drop(q);
}

/* ========== copy to span ========== */

static void test_copy_to_span_writes_front_to_back() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);

    int32_t got[SPREAD_LEN];
    tda_queue_copy_to_span(q, TDA_SPAN_FROM_DATA_MUT(int32_t, got, SPREAD_LEN));

    TEST_ASSERT_EQUAL_INT32_ARRAY(SPREAD, got, SPREAD_LEN);
    assert_elems(q, SPREAD, SPREAD_LEN);

    tda_queue_drop(q);
}

// the contents are at most two runs in the buffer and exactly one run here
static void test_copy_to_span_of_a_wrapped_queue_is_in_order() {
    tda_Queue *q = make_wrapped();

    int32_t got[4];
    tda_queue_copy_to_span(q, TDA_SPAN_FROM_DATA_MUT(int32_t, got, 4));

    constexpr int32_t want[4] = {10, 20, 30, 40};
    TEST_ASSERT_EQUAL_INT32_ARRAY(want, got, 4);

    tda_queue_drop(q);
}

static void test_copy_to_span_of_empty_writes_nothing() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW(int32_t, tda_al_default(), &q));

    int32_t got[2] = {11, 22};
    tda_queue_copy_to_span(q, TDA_SPAN_FROM_DATA_MUT(int32_t, got, 0));

    TEST_ASSERT_EQUAL_INT32(11, got[0]);
    TEST_ASSERT_EQUAL_INT32(22, got[1]);

    tda_queue_drop(q);
}

// the whole point of the one way bridge: algo works on the copy, and the queue keeps
// the arrival order that gives it its meaning
static void test_the_copy_reaches_algo_and_the_queue_is_untouched() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);

    int32_t got[SPREAD_LEN];
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, got, SPREAD_LEN);
    tda_queue_copy_to_span(q, s);

    TEST_ASSERT_EQUAL_size_t(2, tda_span_max_elem(tda_span_mut_to_span(s), tda_cmp_i32));

    tda_span_sort(s, tda_cmp_i32);
    TEST_ASSERT_TRUE(tda_span_is_sorted(tda_span_mut_to_span(s), tda_cmp_i32));

    assert_elems(q, SPREAD, SPREAD_LEN);

    tda_queue_drop(q);
}

/* ========== swap ========== */

static void test_swap_exchanges_the_elems() {
    tda_Queue *a = nullptr;
    tda_Queue *b = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, tda_al_default(), &a, 1, 2, 3));
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, tda_al_default(), &b, 10, 20));

    tda_queue_swap(a, b);

    constexpr int32_t want_a[2] = {10, 20};
    constexpr int32_t want_b[3] = {1, 2, 3};
    assert_elems(a, want_a, 2);
    assert_elems(b, want_b, 3);

    tda_queue_drop(a);
    tda_queue_drop(b);
}

static void test_swap_self_is_noop() {
    tda_Queue *q = make_queue_from(SPREAD, SPREAD_LEN);

    tda_queue_swap(q, q);

    assert_elems(q, SPREAD, SPREAD_LEN);

    tda_queue_drop(q);
}

/* ========== failures ========== */

static void test_new_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 64);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    tda_Queue *q = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_QUEUE_NEW(int32_t, arena, &q));
    TEST_ASSERT_NULL(q);

    tda_al_arena_drop(arena);
}

static void test_from_data_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 128);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Queue *q = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_QUEUE_FROM_DATA(int32_t, SPREAD, 1000, arena, &q));
    TEST_ASSERT_NULL(q);

    tda_al_arena_drop(arena);
}

// a refused push must leave the queue exactly as it was — not a ring with a hole in it
static void test_push_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, arena, &q, 5, 1, 3));
    tda_test_arena_leave(arena, 0);

    constexpr int32_t val = 100;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_queue_push(q, &val));

    constexpr int32_t want[3] = {5, 1, 3};
    assert_elems(q, want, 3);

    tda_queue_drop(q);
    tda_al_arena_drop(arena);
}

static void test_copy_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Queue *src = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, arena, &src, 5, 1, 3));
    tda_test_arena_leave(arena, 0);

    tda_Queue *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_queue_copy(src, &dst));
    TEST_ASSERT_NULL(dst);

    tda_queue_drop(src);
    tda_al_arena_drop(arena);
}

static void test_reserve_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, arena, &q, 5, 1, 3));
    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_queue_reserve(q, 1000));

    constexpr int32_t want[3] = {5, 1, 3};
    assert_elems(q, want, 3);

    tda_queue_drop(q);
    tda_al_arena_drop(arena);
}

// The queue is built in two steps, the deque and then the header that hides it. When the
// second one is refused the first must not be stranded: the probe counts what is still
// live, and an arena would hide the leak because it frees everything at once
static void test_a_refused_header_frees_the_deque() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 1);

    tda_Queue *q = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_QUEUE_NEW(int32_t, &al, &q));

    TEST_ASSERT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the same, one allocation later: from_data takes a buffer as well
static void test_a_refused_header_frees_a_filled_deque() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 2);

    tda_Queue *q = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_QUEUE_FROM_DATA(int32_t, SPREAD, SPREAD_LEN, &al, &q));

    TEST_ASSERT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== compare ========== */

static void test_eq_matches_the_same_elems() {
    constexpr int32_t src[3] = {7, 8, 9};

    tda_Queue *a = make_queue(src, 3);
    tda_Queue *b = make_queue_from(src, 3);

    TEST_ASSERT_TRUE(tda_queue_eq(a, a));
    TEST_ASSERT_TRUE(tda_queue_eq(a, b));
    TEST_ASSERT_TRUE(tda_queue_eq(b, a));
    TEST_ASSERT_TRUE(tda_queue_eq_by(a, b, tda_eq_i32));

    tda_queue_drop(a);
    tda_queue_drop(b);
}

static void test_eq_parts_one_differing_elem() {
    constexpr int32_t lhs[3] = {7, 8, 9};
    constexpr int32_t rhs[3] = {7, 8, 99};

    tda_Queue *a = make_queue(lhs, 3);
    tda_Queue *b = make_queue(rhs, 3);

    TEST_ASSERT_FALSE(tda_queue_eq(a, b));
    TEST_ASSERT_FALSE(tda_queue_eq_by(a, b, tda_eq_i32));

    tda_queue_drop(a);
    tda_queue_drop(b);
}

static void test_eq_parts_different_lengths() {
    constexpr int32_t src[3] = {7, 8, 9};

    tda_Queue *a = make_queue(src, 3);
    tda_Queue *shorter = make_queue(src, 2);

    TEST_ASSERT_FALSE(tda_queue_eq(a, shorter));
    TEST_ASSERT_FALSE(tda_queue_eq(shorter, a));

    tda_queue_drop(a);
    tda_queue_drop(shorter);
}

static void test_eq_of_two_empties() {
    constexpr int32_t src[1] = {7};

    tda_Queue *a = make_queue(src, 0);
    tda_Queue *b = make_queue_from(src, 0);
    tda_Queue *one = make_queue(src, 1);

    TEST_ASSERT_TRUE(tda_queue_eq(a, b));
    TEST_ASSERT_TRUE(tda_queue_eq_by(a, b, tda_eq_i32));
    TEST_ASSERT_FALSE(tda_queue_eq(a, one));

    tda_queue_drop(a);
    tda_queue_drop(b);
    tda_queue_drop(one);
}

// the ring under the queue may be split or not; the queue order is what is compared
static void test_eq_ignores_where_the_ring_starts() {
    constexpr int32_t want[4] = {10, 20, 30, 40};

    tda_Queue *straight = make_queue_from(want, 4);
    tda_Queue *wrapped = make_wrapped();

    TEST_ASSERT_TRUE(tda_queue_eq(straight, wrapped));
    TEST_ASSERT_TRUE(tda_queue_eq(wrapped, straight));
    TEST_ASSERT_TRUE(tda_queue_eq_by(wrapped, straight, tda_eq_i32));

    tda_queue_drop(straight);
    tda_queue_drop(wrapped);
}

// a queue is compared front to back: the same elems in another order are another queue
static void test_eq_is_order_sensitive() {
    constexpr int32_t src[3] = {7, 8, 9};

    tda_Queue *a = make_queue(src, 3);
    tda_Queue *b = make_queue(src, 3);

    const int32_t front = *TDA_QUEUE_FRONT_AS(int32_t, b);
    tda_queue_pop(b);
    push_int(b, front);

    TEST_ASSERT_EQUAL_size_t(tda_queue_len(a), tda_queue_len(b));
    TEST_ASSERT_FALSE(tda_queue_eq(a, b));

    tda_queue_drop(a);
    tda_queue_drop(b);
}

static void test_eq_by_asks_the_equality() {
    constexpr Pair lhs[2] = {{1, 10}, {2, 20}};
    constexpr Pair rhs[2] = {{1, 70}, {2, 80}};

    tda_Queue *a = nullptr;
    tda_Queue *b = nullptr;
    TDA_TEST_OK(TDA_QUEUE_FROM_DATA(Pair, lhs, 2, tda_al_default(), &a));
    TDA_TEST_OK(TDA_QUEUE_FROM_DATA(Pair, rhs, 2, tda_al_default(), &b));

    TEST_ASSERT_FALSE(tda_queue_eq(a, b));
    TEST_ASSERT_TRUE(tda_queue_eq_by(a, b, tda_test_pair_eq_a));

    tda_queue_drop(a);
    tda_queue_drop(b);
}

/* ========== into ========== */

// the deque was there all along, ring and all: taking it copies nothing
static void test_into_deque_hands_the_elems_over() {
    constexpr int32_t want[4] = {10, 20, 30, 40};
    tda_Queue *q = make_wrapped();

    const size_t cap = tda_queue_cap(q);
    tda_Deque *d = tda_queue_into_deque(q);

    TEST_ASSERT_EQUAL_size_t(4, tda_deque_len(d));
    TEST_ASSERT_EQUAL_size_t(cap, tda_deque_cap(d));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_deque_al(d));

    // the elems keep their queue order even though the ring underneath is split
    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT32(want[i], *TDA_DEQUE_GET_AS(int32_t, d, i));
    }

    tda_deque_drop(d);
}

static void test_into_deque_of_an_empty_queue() {
    constexpr int32_t src[1] = {7};
    tda_Queue *q = make_queue(src, 0);

    tda_Deque *d = tda_queue_into_deque(q);

    TEST_ASSERT_EQUAL_size_t(0, tda_deque_len(d));

    tda_deque_drop(d);
}

static void test_into_deque_releases_the_header_alone() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_NEW(int32_t, &al, &q));

    // the last block the constructor took is the adapter's own header, so this is what
    // into has to hand back, and with the size it was taken as
    const size_t header = probe.last_alloc_size;

    TDA_TEST_OK(TDA_QUEUE_PUSH(int32_t, q, 1));
    const size_t live = probe.live;

    tda_Deque *d = tda_queue_into_deque(q);

    TEST_ASSERT_EQUAL_size_t(live - 1, probe.live);
    TEST_ASSERT_EQUAL_size_t(header, probe.last_dealloc_size);

    tda_deque_drop(d);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== print ========== */

// a printer writes to a stream, so a case reads one back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_Queue *q) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_queue_fprint(q, stream, tda_fprint_i32);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof buf - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_elems() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, tda_al_default(), &q, 5, 3, 1));

    assert_prints("[5, 3, 1]\n", q);

    tda_queue_drop(q);
}

static void test_fprint_of_a_single_elem_has_no_separator() {
    tda_Queue *q = nullptr;
    TDA_TEST_OK(TDA_QUEUE_OF(int32_t, tda_al_default(), &q, 7));

    assert_prints("[7]\n", q);

    tda_queue_drop(q);
}

static void test_fprint_of_an_empty_queue() {
    tda_Queue *q = make_queue(nullptr, 0);

    assert_prints("[]\n", q);

    tda_queue_drop(q);
}

// the stdout twin takes no stream and cannot be captured portably, so a case can only
// say that it runs and reaches the same printer
static void test_print_writes_to_stdout() {
    constexpr int32_t src[3] = {1, 2, 3};
    tda_Queue *q = make_queue(src, 3);

    tda_queue_print(q, tda_fprint_i32);

    tda_queue_drop(q);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_starts_empty);
    RUN_TEST(test_new_cap_reserves_without_length);
    RUN_TEST(test_from_data_keeps_arrival_order);
    RUN_TEST(test_from_data_empty_stays_empty);
    RUN_TEST(test_from_span_copies_the_view);
    RUN_TEST(test_from_data_copies_whole_elems);
    RUN_TEST(test_drop_null_is_noop);
    RUN_TEST(test_drop_hands_back_everything_it_took);

    RUN_TEST(test_pushes_leave_in_the_order_they_arrived);
    RUN_TEST(test_order_survives_growth);
    RUN_TEST(test_pushing_and_popping_in_step_stays_in_order);
    RUN_TEST(test_pushes_after_a_full_drain_are_ordered_again);
    RUN_TEST(test_duplicates_all_come_back);
    RUN_TEST(test_a_wrapped_queue_drains_in_order);
    RUN_TEST(test_a_wrapped_queue_keeps_its_order_through_growth);

    RUN_TEST(test_front_reads_without_removing);
    RUN_TEST(test_back_follows_the_last_push);
    RUN_TEST(test_the_ends_are_writable);
    RUN_TEST(test_front_and_back_meet_on_a_single_elem);

    RUN_TEST(test_len_follows_push_and_pop);
    RUN_TEST(test_pop_leaves_the_capacity_alone);

    RUN_TEST(test_clear_empties_without_giving_back_the_room);
    RUN_TEST(test_clear_leaves_a_usable_queue);
    RUN_TEST(test_reserve_grows_the_room_only);
    RUN_TEST(test_reserve_below_the_capacity_changes_nothing);
    RUN_TEST(test_shrink_to_fit_keeps_the_order);
    RUN_TEST(test_shrink_to_fit_unwraps_what_it_moves);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_inherits_the_allocator);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_empties_the_source);
    RUN_TEST(test_move_assign_across_allocators_reports_an_exhausted_arena);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_of_a_wrapped_queue_keeps_the_order);
    RUN_TEST(test_copy_of_empty_stays_empty);
    RUN_TEST(test_copy_assign_overwrites_the_target);
    RUN_TEST(test_copy_assign_self_is_noop);

    RUN_TEST(test_copy_to_span_writes_front_to_back);
    RUN_TEST(test_copy_to_span_of_a_wrapped_queue_is_in_order);
    RUN_TEST(test_copy_to_span_of_empty_writes_nothing);
    RUN_TEST(test_the_copy_reaches_algo_and_the_queue_is_untouched);

    RUN_TEST(test_swap_exchanges_the_elems);
    RUN_TEST(test_swap_self_is_noop);

    RUN_TEST(test_new_reports_an_exhausted_arena);
    RUN_TEST(test_from_data_reports_an_exhausted_arena);
    RUN_TEST(test_push_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_copy_reports_an_exhausted_arena);
    RUN_TEST(test_reserve_reports_an_exhausted_arena);
    RUN_TEST(test_a_refused_header_frees_the_deque);
    RUN_TEST(test_a_refused_header_frees_a_filled_deque);


    RUN_TEST(test_eq_matches_the_same_elems);
    RUN_TEST(test_eq_parts_one_differing_elem);
    RUN_TEST(test_eq_parts_different_lengths);
    RUN_TEST(test_eq_of_two_empties);
    RUN_TEST(test_eq_ignores_where_the_ring_starts);
    RUN_TEST(test_eq_is_order_sensitive);
    RUN_TEST(test_eq_by_asks_the_equality);


    RUN_TEST(test_into_deque_hands_the_elems_over);
    RUN_TEST(test_into_deque_of_an_empty_queue);
    RUN_TEST(test_into_deque_releases_the_header_alone);

    RUN_TEST(test_fprint_writes_the_elems);
    RUN_TEST(test_fprint_of_a_single_elem_has_no_separator);
    RUN_TEST(test_fprint_of_an_empty_queue);
    RUN_TEST(test_print_writes_to_stdout);

    return UNITY_END();
}
