#include "tda/ds/hset.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/core/print.h"
#include "tda/core/util.h"

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

// every key in one bucket, so the chain is walked instead of hit at its head
static tda_Hash hash_all_alike(const void *x) {
    TDA_UNUSED(x);

    return 0;
}

static tda_Hash hash_pair(const void *x) {
    const Pair *p = x;

    return tda_hash_combine(tda_hash_i64(&p->a), tda_hash_i64(&p->b));
}

static bool eq_pair(const void *lhs, const void *rhs) {
    const Pair *a = lhs;
    const Pair *b = rhs;

    return a->a == b->a && a->b == b->b;
}

[[nodiscard]]
static size_t align_up_to(size_t val, size_t alignment) {
    return (val + alignment - 1) & ~(alignment - 1);
}

[[nodiscard]]
static tda_HSet *make_set(tda_Hasher hasher) {
    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, hasher, tda_eq_i32, tda_al_default(), &s));

    return s;
}

static void put(tda_HSet *s, int32_t key) {
    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, s, key, nullptr));
}

[[nodiscard]]
static tda_HSet *make_filled(tda_Hasher hasher, int32_t n) {
    tda_HSet *s = make_set(hasher);
    for (int32_t i = 0; i < n; ++i) {
        put(s, i);
    }
    return s;
}

static void assert_has(const tda_HSet *s, int32_t key) {
    TEST_ASSERT_TRUE(TDA_HSET_CONTAINS(int32_t, s, key));
    TEST_ASSERT_NOT_NULL(TDA_HSET_FIND(int32_t, s, key));
}

static void assert_missing(const tda_HSet *s, int32_t key) {
    TEST_ASSERT_FALSE(TDA_HSET_CONTAINS(int32_t, s, key));
    TEST_ASSERT_NULL(TDA_HSET_FIND(int32_t, s, key));
}

// the walk reaches exactly 'len' keys and every one of them is in the set
static void assert_walk_sees_everything(const tda_HSet *s) {
    size_t seen = 0;
    TDA_HSET_FOR_EACH(node, s) {
        assert_has(s, *TDA_HSET_NODE_KEY_AS(int32_t, node));
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(tda_hset_len(s), seen);
}

/* ========== the node layout ========== */

// the whole reason ds/hset goes through internal/hmap_impl.h rather than storing a dummy
// byte: with no value behind the key there is nothing to pad the key to, so a set's node
// is a map's node minus the value AND minus the padding that would have aligned it
static void test_a_set_node_is_a_map_node_without_the_value() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, &al, &s));
    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, s, 1, nullptr));
    const size_t set_node = probe.last_alloc_size; // the node is the last block a first insert takes

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &m));
    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, m, 1, 10, nullptr));
    const size_t map_node = probe.last_alloc_size;

    const size_t padding = align_up_to(sizeof(int32_t), alignof(max_align_t)) - sizeof(int32_t);
    TEST_ASSERT_TRUE(set_node < map_node);
    TEST_ASSERT_EQUAL_size_t(set_node + padding + sizeof(int32_t), map_node);

    tda_hset_drop(s);
    tda_hmap_drop(m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== lifetime ========== */

static void test_new_starts_empty() {
    tda_HSet *s = make_set(tda_hash_i32);

    TEST_ASSERT_EQUAL_size_t(0, tda_hset_len(s));
    TEST_ASSERT_EQUAL_size_t(0, tda_hset_bucket_count(s));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_hset_key_size(s));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_hset_al(s));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hset_hasher(s));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hset_key_eq(s));
    TEST_ASSERT_NULL(tda_hset_first_node(s));

    tda_hset_drop(s);
}

static void test_new_cap_reserves_buckets_without_keys() {
    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW_CAP(int32_t, 100, tda_hash_i32, tda_eq_i32, tda_al_default(), &s));

    TEST_ASSERT_EQUAL_size_t(0, tda_hset_len(s));
    TEST_ASSERT_TRUE(tda_hset_bucket_count(s) >= 100);

    tda_hset_drop(s);
}

static void test_drop_null_is_noop() {
    tda_hset_drop(nullptr);
}

// three blocks go into a filled set — the set header, the map behind it, and the buckets —
// plus one per key, and drop must hand back all of them
static void test_drop_hands_back_everything() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, &al, &s));
    for (int32_t i = 0; i < 20; ++i) {
        put(s, i);
    }

    tda_hset_drop(s);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the set header is taken after the map, and a refusal of it must not strand the map
static void test_a_refused_header_frees_the_map() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 1);

    tda_HSet *s = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, &al, &s));

    TEST_ASSERT_NULL(s);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== insert and lookup ========== */

static void test_insert_then_contains() {
    tda_HSet *s = make_set(tda_hash_i32);

    put(s, 1);
    put(s, 2);

    assert_has(s, 1);
    assert_has(s, 2);
    assert_missing(s, 3);
    TEST_ASSERT_EQUAL_size_t(2, tda_hset_len(s));

    tda_hset_drop(s);
}

static void test_contains_on_an_empty_set_is_false() {
    tda_HSet *s = make_set(tda_hash_i32);

    assert_missing(s, 1);

    tda_hset_drop(s);
}

// a set holds a key once however many times it is put in
static void test_inserting_a_key_twice_keeps_one_entry() {
    tda_HSet *s = make_set(tda_hash_i32);

    put(s, 1);
    put(s, 1);
    put(s, 1);

    TEST_ASSERT_EQUAL_size_t(1, tda_hset_len(s));
    assert_has(s, 1);

    tda_hset_drop(s);
}

static void test_insert_reports_whether_the_key_was_new() {
    tda_HSet *s = make_set(tda_hash_i32);

    bool is_new = false;
    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, s, 1, &is_new));
    TEST_ASSERT_TRUE(is_new);

    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, s, 1, &is_new));
    TEST_ASSERT_FALSE(is_new);

    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, s, 2, &is_new));
    TEST_ASSERT_TRUE(is_new);

    tda_hset_drop(s);
}

static void test_find_gives_the_key_back() {
    tda_HSet *s = make_filled(tda_hash_i32, 5);

    const tda_HSetNode *node = TDA_HSET_FIND(int32_t, s, 3);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT32(3, *TDA_HSET_NODE_KEY_AS(int32_t, node));

    tda_hset_drop(s);
}

static void test_a_set_whose_keys_all_collide_still_finds_them() {
    tda_HSet *s = make_filled(hash_all_alike, 20);

    TEST_ASSERT_EQUAL_size_t(20, tda_hset_len(s));
    for (int32_t i = 0; i < 20; ++i) {
        assert_has(s, i);
    }
    assert_missing(s, 20);

    tda_hset_drop(s);
}

static void test_wide_keys_travel_whole() {
    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(Pair, hash_pair, eq_pair, tda_al_default(), &s));

    constexpr Pair a = {1, 2};
    constexpr Pair b = {1, 3};
    TDA_TEST_OK(tda_hset_insert(s, &a, nullptr));

    TEST_ASSERT_TRUE(tda_hset_contains(s, &a));
    TEST_ASSERT_FALSE(tda_hset_contains(s, &b)); // the second field is part of the key
    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_hset_key_size(s));

    tda_hset_drop(s);
}

/* ========== growth ========== */

static void test_every_key_survives_the_growths() {
    tda_HSet *s = make_filled(tda_hash_i32, 300);

    TEST_ASSERT_EQUAL_size_t(300, tda_hset_len(s));
    for (int32_t i = 0; i < 300; ++i) {
        assert_has(s, i);
    }
    assert_missing(s, 300);

    tda_hset_drop(s);
}

static void test_a_borrowed_node_survives_every_growth() {
    tda_HSet *s = make_filled(tda_hash_i32, 4);

    const tda_HSetNode *held[4];
    for (int32_t i = 0; i < 4; ++i) {
        held[i] = TDA_HSET_FIND(int32_t, s, i);
        TEST_ASSERT_NOT_NULL(held[i]);
    }

    for (int32_t i = 4; i < 500; ++i) {
        put(s, i);
    }

    for (int32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_PTR(held[i], TDA_HSET_FIND(int32_t, s, i));
        TEST_ASSERT_EQUAL_INT32(i, *TDA_HSET_NODE_KEY_AS(int32_t, held[i]));
    }

    tda_hset_drop(s);
}

static void test_reserve_grows_the_buckets_only() {
    tda_HSet *s = make_filled(tda_hash_i32, 4);

    TDA_TEST_OK(tda_hset_reserve(s, 1000));

    TEST_ASSERT_TRUE(tda_hset_bucket_count(s) >= 1000);
    TEST_ASSERT_EQUAL_size_t(4, tda_hset_len(s));
    assert_has(s, 3);

    tda_hset_drop(s);
}

/* ========== remove ========== */

static void test_remove_takes_the_key_out() {
    tda_HSet *s = make_filled(tda_hash_i32, 5);

    TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, s, 2));

    TEST_ASSERT_EQUAL_size_t(4, tda_hset_len(s));
    assert_missing(s, 2);
    assert_has(s, 1);
    assert_has(s, 3);

    tda_hset_drop(s);
}

static void test_remove_of_a_missing_key_says_so() {
    tda_HSet *s = make_filled(tda_hash_i32, 3);

    TEST_ASSERT_FALSE(TDA_HSET_REMOVE(int32_t, s, 99));
    TEST_ASSERT_EQUAL_size_t(3, tda_hset_len(s));

    tda_hset_drop(s);
}

static void test_remove_from_the_middle_of_a_chain() {
    tda_HSet *s = make_filled(hash_all_alike, 5);

    TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, s, 2));

    TEST_ASSERT_EQUAL_size_t(4, tda_hset_len(s));
    assert_missing(s, 2);
    for (int32_t i = 0; i < 5; ++i) {
        if (i != 2) {
            assert_has(s, i);
        }
    }

    tda_hset_drop(s);
}

// a set node carries no value, so the mutable walk is there to remove through: one pass
// takes out every even key and leaves the rest
static void test_mut_walk_removes_through_the_nodes() {
    tda_HSet *s = make_filled(tda_hash_i32, 6);

    tda_HSetNode *node = tda_hset_first_node_mut(s);
    while (node) {
        const int32_t key = *TDA_HSET_NODE_KEY_AS(int32_t, node);
        tda_HSetNode *next = tda_hset_node_next_mut(s, node);
        if (key % 2 == 0) {
            tda_hset_remove_node(s, node);
        }
        node = next;
    }

    TEST_ASSERT_EQUAL_size_t(3, tda_hset_len(s));
    assert_has(s, 1);
    assert_has(s, 3);
    assert_has(s, 5);
    assert_missing(s, 0);
    assert_missing(s, 2);
    assert_missing(s, 4);

    tda_hset_drop(s);
}

static void test_mut_walk_of_an_empty_set_stops_at_once() {
    tda_HSet *s = make_set(tda_hash_i32);

    TEST_ASSERT_NULL(tda_hset_first_node_mut(s));

    tda_hset_drop(s);
}

static void test_remove_node_drops_the_key_it_names() {
    tda_HSet *s = make_filled(hash_all_alike, 4);

    tda_HSetNode *node = TDA_HSET_FIND_MUT(int32_t, s, 2);
    TEST_ASSERT_NOT_NULL(node);

    tda_hset_remove_node(s, node);

    TEST_ASSERT_EQUAL_size_t(3, tda_hset_len(s));
    assert_missing(s, 2);
    assert_has(s, 3);

    tda_hset_drop(s);
}

static void test_a_key_can_be_put_back_after_removal() {
    tda_HSet *s = make_filled(tda_hash_i32, 3);

    TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, s, 1));
    put(s, 1);

    TEST_ASSERT_EQUAL_size_t(3, tda_hset_len(s));
    assert_has(s, 1);

    tda_hset_drop(s);
}

static void test_clear_empties_and_keeps_the_buckets() {
    tda_HSet *s = make_filled(tda_hash_i32, 20);
    const size_t buckets = tda_hset_bucket_count(s);

    tda_hset_clear(s);

    TEST_ASSERT_EQUAL_size_t(0, tda_hset_len(s));
    TEST_ASSERT_EQUAL_size_t(buckets, tda_hset_bucket_count(s));
    TEST_ASSERT_NULL(tda_hset_first_node(s));
    assert_missing(s, 1);

    tda_hset_drop(s);
}

static void test_clear_leaves_a_usable_set() {
    tda_HSet *s = make_filled(tda_hash_i32, 10);

    tda_hset_clear(s);
    put(s, 5);

    TEST_ASSERT_EQUAL_size_t(1, tda_hset_len(s));
    assert_has(s, 5);

    tda_hset_drop(s);
}

/* ========== walk ========== */

static void test_the_walk_reaches_every_key_once() {
    tda_HSet *s = make_filled(tda_hash_i32, 50);

    assert_walk_sees_everything(s);

    tda_hset_drop(s);
}

static void test_the_walk_crosses_a_single_chain() {
    tda_HSet *s = make_filled(hash_all_alike, 12);

    assert_walk_sees_everything(s);

    tda_hset_drop(s);
}

static void test_the_walk_after_a_removal_sees_the_rest() {
    tda_HSet *s = make_filled(tda_hash_i32, 30);

    for (int32_t i = 0; i < 30; i += 2) {
        TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, s, i));
    }

    TEST_ASSERT_EQUAL_size_t(15, tda_hset_len(s));
    assert_walk_sees_everything(s);

    tda_hset_drop(s);
}

static void test_shrink_to_fit_gives_the_buckets_back() {
    tda_HSet *s = make_filled(tda_hash_i32, 300);
    const size_t grown = tda_hset_bucket_count(s);

    for (int32_t i = 3; i < 300; ++i) {
        TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, s, i));
    }

    TDA_TEST_OK(tda_hset_shrink_to_fit(s));

    TEST_ASSERT_TRUE(tda_hset_bucket_count(s) < grown);
    TEST_ASSERT_TRUE(tda_hset_bucket_count(s) >= tda_hset_len(s));
    for (int32_t i = 0; i < 3; ++i) {
        assert_has(s, i);
    }
    assert_walk_sees_everything(s);

    tda_hset_drop(s);
}

static void test_shrink_to_fit_of_an_empty_set_owns_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, &al, &s));
    for (int32_t i = 0; i < 50; ++i) {
        put(s, i);
    }
    tda_hset_clear(s);

    TDA_TEST_OK(tda_hset_shrink_to_fit(s));

    TEST_ASSERT_EQUAL_size_t(0, tda_hset_bucket_count(s));
    TEST_ASSERT_EQUAL_size_t(2, probe.live); // the set header and the map behind it

    put(s, 1); // still a set: the next key takes buckets again
    assert_has(s, 1);

    tda_hset_drop(s);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// shrinking relinks the nodes where they lie, so a borrowed one comes through it
static void test_shrink_to_fit_keeps_borrowed_nodes() {
    tda_HSet *s = make_filled(tda_hash_i32, 200);

    const tda_HSetNode *held = TDA_HSET_FIND(int32_t, s, 1);
    TEST_ASSERT_NOT_NULL(held);

    for (int32_t i = 3; i < 200; ++i) {
        TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, s, i));
    }
    TDA_TEST_OK(tda_hset_shrink_to_fit(s));

    TEST_ASSERT_EQUAL_PTR(held, TDA_HSET_FIND(int32_t, s, 1));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_HSET_NODE_KEY_AS(int32_t, held));

    tda_hset_drop(s);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_HSet *src = make_filled(tda_hash_i32, 10);

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(tda_hset_copy(src, &dst));

    TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, dst, 1));
    put(dst, 100);

    assert_has(src, 1);
    assert_missing(src, 100);
    assert_missing(dst, 1);
    assert_has(dst, 100);

    tda_hset_drop(src);
    tda_hset_drop(dst);
}

static void test_copy_carries_the_hasher_and_the_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *src = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, hash_all_alike, tda_eq_i32, arena, &src));
    put(src, 1);

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(tda_hset_copy(src, &dst));

    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hset_hasher(dst));
    TEST_ASSERT_EQUAL_PTR(arena, tda_hset_al(dst));
    assert_has(dst, 1);

    tda_hset_drop(src);
    tda_hset_drop(dst);
    tda_al_arena_drop(arena);
}

// a copy of a valueless map goes through the same private door the set was built with,
// so the clone keeps the small node too. The copy's own last block is the set header, so
// the size is read from the next key put into the clone instead
static void test_a_copy_keeps_the_smaller_node() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HSet *src = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, &al, &src));
    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, src, 1, nullptr));
    const size_t node = probe.last_alloc_size;

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(tda_hset_copy(src, &dst));
    assert_has(dst, 1);

    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, dst, 2, nullptr)); // fits the buckets already there
    TEST_ASSERT_EQUAL_size_t(node, probe.last_alloc_size);

    tda_hset_drop(src);
    tda_hset_drop(dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *src = make_filled(tda_hash_i32, 8);

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(tda_hset_copy_with(src, arena, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_hset_al(dst));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_hset_al(src));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hset_hasher(dst));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hset_key_eq(dst));
    TEST_ASSERT_TRUE(tda_hset_eq(src, dst));

    // the source is gone and the copy still answers: the nodes are its own
    tda_hset_drop(src);
    assert_has(dst, 3);

    tda_hset_drop(dst);
    tda_al_arena_drop(arena);
}

// the buckets and the nodes are asked of the allocator the copy is going to
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    tda_HSet *src = make_filled(tda_hash_i32, 8);

    tda_HSet *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hset_copy_with(src, arena, &dst));
    TEST_ASSERT_NULL(dst);
    TEST_ASSERT_EQUAL_size_t(8, tda_hset_len(src));

    tda_hset_drop(src);
    tda_al_arena_drop(arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HSet *src = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, hash_all_alike, tda_eq_i32, &al, &src));
    put(src, 1);
    put(src, 2);

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, &al, &dst));
    put(dst, 9);

    const size_t requests = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_hset_move_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(2, tda_hset_len(dst));
    assert_has(dst, 1);
    assert_has(dst, 2);
    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hset_hasher(dst));

    // the source gives back everything, its buckets included, as every container does
    TEST_ASSERT_EQUAL_size_t(0, tda_hset_len(src));
    TEST_ASSERT_EQUAL_size_t(0, tda_hset_bucket_count(src));

    // and is still a set: the first insert makes its buckets anew
    put(src, 5);
    assert_has(src, 5);

    tda_hset_drop(src);
    tda_hset_drop(dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_move_assign_across_allocators_empties_the_source() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *src = make_filled(tda_hash_i32, 8);

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, hash_all_alike, tda_eq_i32, arena, &dst));
    put(dst, 9);

    TDA_TEST_OK(tda_hset_move_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(8, tda_hset_len(dst));
    assert_has(dst, 3);
    assert_missing(dst, 9);
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hset_hasher(dst));
    TEST_ASSERT_EQUAL_PTR(arena, tda_hset_al(dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_hset_len(src));
    TEST_ASSERT_EQUAL_size_t(0, tda_hset_bucket_count(src));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_hset_al(src));

    tda_hset_drop(src);
    tda_hset_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_across_allocators_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, arena, &dst));
    put(dst, 9);
    tda_test_arena_leave(arena, 0);

    tda_HSet *src = make_filled(tda_hash_i32, 8);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hset_move_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(8, tda_hset_len(src));
    TEST_ASSERT_EQUAL_size_t(1, tda_hset_len(dst));
    assert_has(dst, 9);

    tda_hset_drop(src);
    tda_hset_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_HSet *s = make_filled(tda_hash_i32, 4);

    TDA_TEST_OK(tda_hset_move_assign(s, s));

    TEST_ASSERT_EQUAL_size_t(4, tda_hset_len(s));
    assert_has(s, 2);

    tda_hset_drop(s);
}

static void test_copy_of_empty_stays_empty() {
    tda_HSet *src = make_set(tda_hash_i32);

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(tda_hset_copy(src, &dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_hset_len(dst));

    tda_hset_drop(src);
    tda_hset_drop(dst);
}

static void test_copy_assign_overwrites_the_target() {
    tda_HSet *src = make_filled(tda_hash_i32, 6);
    tda_HSet *dst = make_filled(tda_hash_i32, 2);
    put(dst, 100);

    TDA_TEST_OK(tda_hset_copy_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(6, tda_hset_len(dst));
    assert_missing(dst, 100);
    for (int32_t i = 0; i < 6; ++i) {
        assert_has(dst, i);
    }

    tda_hset_drop(src);
    tda_hset_drop(dst);
}

static void test_copy_assign_self_is_noop() {
    tda_HSet *s = make_filled(tda_hash_i32, 5);

    TDA_TEST_OK(tda_hset_copy_assign(s, s));

    TEST_ASSERT_EQUAL_size_t(5, tda_hset_len(s));
    assert_has(s, 3);

    tda_hset_drop(s);
}

/* ========== swap ========== */

static void test_swap_exchanges_the_keys_and_the_hashers() {
    tda_HSet *a = make_set(tda_hash_i32);
    put(a, 1);

    tda_HSet *b = make_set(hash_all_alike);
    put(b, 2);
    put(b, 3);

    tda_hset_swap(a, b);

    TEST_ASSERT_EQUAL_size_t(2, tda_hset_len(a));
    TEST_ASSERT_EQUAL_size_t(1, tda_hset_len(b));
    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hset_hasher(a));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hset_hasher(b));
    assert_has(a, 2);
    assert_has(b, 1);

    tda_hset_drop(a);
    tda_hset_drop(b);
}

static void test_swap_keeps_the_nodes_alive() {
    tda_HSet *a = make_filled(tda_hash_i32, 4);
    tda_HSet *b = make_set(tda_hash_i32);

    const tda_HSetNode *held = TDA_HSET_FIND(int32_t, a, 2);
    TEST_ASSERT_NOT_NULL(held);

    tda_hset_swap(a, b);

    TEST_ASSERT_EQUAL_PTR(held, TDA_HSET_FIND(int32_t, b, 2));

    tda_hset_drop(a);
    tda_hset_drop(b);
}

static void test_swap_self_is_noop() {
    tda_HSet *s = make_filled(tda_hash_i32, 3);

    tda_hset_swap(s, s);

    TEST_ASSERT_EQUAL_size_t(3, tda_hset_len(s));
    assert_has(s, 1);

    tda_hset_drop(s);
}

/* ========== allocation failure ========== */

static void test_new_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 64);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    tda_HSet *s = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, arena, &s));
    TEST_ASSERT_NULL(s);

    tda_al_arena_drop(arena);
}

static void test_insert_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, arena, &s));
    put(s, 1);
    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HSET_INSERT(int32_t, s, 2, nullptr));

    TEST_ASSERT_EQUAL_size_t(1, tda_hset_len(s));
    assert_has(s, 1);
    assert_missing(s, 2);

    tda_al_arena_drop(arena);
}

// putting a key that is already there needs no memory, so an exhausted arena is no
// obstacle to it
static void test_a_repeat_insert_needs_no_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, arena, &s));
    put(s, 1);
    tda_test_arena_leave(arena, 0);

    bool is_new = true;
    TDA_TEST_OK(TDA_HSET_INSERT(int32_t, s, 1, &is_new));
    TEST_ASSERT_FALSE(is_new);

    tda_al_arena_drop(arena);
}

static void test_reserve_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *s = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, arena, &s));
    put(s, 1);
    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hset_reserve(s, 100000));

    TEST_ASSERT_EQUAL_size_t(1, tda_hset_len(s));
    assert_has(s, 1);

    tda_al_arena_drop(arena);
}

static void test_copy_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *src = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, arena, &src));
    put(src, 1);
    tda_test_arena_leave(arena, 0);

    tda_HSet *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hset_copy(src, &dst));
    TEST_ASSERT_NULL(dst);

    tda_al_arena_drop(arena);
}

static void test_copy_assign_leaves_the_target_untouched_on_failure() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HSet *src = make_filled(tda_hash_i32, 50);

    tda_HSet *dst = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW(int32_t, tda_hash_i32, tda_eq_i32, arena, &dst));
    put(dst, 7);
    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hset_copy_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(1, tda_hset_len(dst));
    assert_has(dst, 7);

    tda_hset_drop(src);
    tda_al_arena_drop(arena);
}

/* ========== compare ========== */

static void test_eq_ignores_insertion_order_and_bucket_count() {
    tda_HSet *a = make_filled(tda_hash_i32, 40);

    tda_HSet *b = nullptr;
    TDA_TEST_OK(TDA_HSET_NEW_CAP(int32_t, 256, tda_hash_i32, tda_eq_i32, tda_al_default(), &b));
    for (int32_t i = 39; i >= 0; --i) {
        put(b, i);
    }

    TEST_ASSERT_TRUE(tda_hset_bucket_count(a) != tda_hset_bucket_count(b));
    TEST_ASSERT_TRUE(tda_hset_eq(a, a));
    TEST_ASSERT_TRUE(tda_hset_eq(a, b));
    TEST_ASSERT_TRUE(tda_hset_eq(b, a));

    tda_hset_drop(a);
    tda_hset_drop(b);
}

// the keys of 'a' are looked up in 'b', so the hasher that answers is the one of 'b'
static void test_eq_looks_the_keys_up_with_the_hasher_of_the_other() {
    tda_HSet *a = make_filled(tda_hash_i32, 12);
    tda_HSet *b = make_filled(hash_all_alike, 12);

    TEST_ASSERT_TRUE(tda_hset_eq(a, b));
    TEST_ASSERT_TRUE(tda_hset_eq(b, a));

    tda_hset_drop(a);
    tda_hset_drop(b);
}

// the same number of keys, one in place of another
static void test_eq_parts_a_differing_key() {
    tda_HSet *a = make_filled(tda_hash_i32, 8);
    tda_HSet *b = make_filled(tda_hash_i32, 8);
    TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, b, 3));
    put(b, 100);

    TEST_ASSERT_EQUAL_size_t(tda_hset_len(a), tda_hset_len(b));
    TEST_ASSERT_FALSE(tda_hset_eq(a, b));
    TEST_ASSERT_FALSE(tda_hset_eq(b, a));

    tda_hset_drop(a);
    tda_hset_drop(b);
}

// one is a proper subset of the other, so only the length says no
static void test_eq_parts_different_lengths() {
    tda_HSet *a = make_filled(tda_hash_i32, 8);
    tda_HSet *smaller = make_filled(tda_hash_i32, 7);

    TEST_ASSERT_FALSE(tda_hset_eq(a, smaller));
    TEST_ASSERT_FALSE(tda_hset_eq(smaller, a));

    tda_hset_drop(a);
    tda_hset_drop(smaller);
}

static void test_eq_of_two_empties() {
    tda_HSet *a = make_set(tda_hash_i32);
    tda_HSet *b = make_filled(tda_hash_i32, 8);
    tda_HSet *one = make_filled(tda_hash_i32, 1);

    tda_hset_clear(b);

    TEST_ASSERT_TRUE(tda_hset_eq(a, b));
    TEST_ASSERT_TRUE(tda_hset_eq(b, a));
    TEST_ASSERT_FALSE(tda_hset_eq(a, one));

    tda_hset_drop(a);
    tda_hset_drop(b);
    tda_hset_drop(one);
}

static void test_eq_walks_whole_chains() {
    tda_HSet *a = make_filled(hash_all_alike, 16);
    tda_HSet *b = make_filled(hash_all_alike, 16);
    TEST_ASSERT_TRUE(TDA_HSET_REMOVE(int32_t, b, 15));
    put(b, 100);

    TEST_ASSERT_TRUE(tda_hset_eq(a, a));
    TEST_ASSERT_FALSE(tda_hset_eq(a, b));

    tda_hset_drop(a);
    tda_hset_drop(b);
}

static void test_eq_matches_a_copy() {
    tda_HSet *a = make_filled(tda_hash_i32, 24);

    tda_HSet *copy = nullptr;
    TDA_TEST_OK(tda_hset_copy(a, &copy));

    TEST_ASSERT_TRUE(tda_hset_eq(a, copy));

    tda_hset_drop(a);
    tda_hset_drop(copy);
}

/* ========== print ========== */

// a printer writes to a stream, so a case reads one back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_HSet *s) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_hset_fprint(s, stream, tda_fprint_i32);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof buf - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_key() {
    tda_HSet *s = make_set(tda_hash_i32);
    put(s, 1);

    assert_prints("{1}\n", s);

    tda_hset_drop(s);
}

// with more than one key the order is the buckets', which the header calls unspecified —
// so a case may say what is printed only where there is nothing to order
static void test_fprint_of_an_empty_set() {
    tda_HSet *s = make_set(tda_hash_i32);

    assert_prints("{}\n", s);

    tda_hset_drop(s);
}

// the stdout twin takes no stream and cannot be captured portably, so a case can only
// say that it runs and reaches the same printer
static void test_print_writes_to_stdout() {
    tda_HSet *s = make_set(tda_hash_i32);
    put(s, 1);
    put(s, 2);

    tda_hset_print(s, tda_fprint_i32);

    tda_hset_drop(s);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_a_set_node_is_a_map_node_without_the_value);

    RUN_TEST(test_new_starts_empty);
    RUN_TEST(test_new_cap_reserves_buckets_without_keys);
    RUN_TEST(test_drop_null_is_noop);
    RUN_TEST(test_drop_hands_back_everything);
    RUN_TEST(test_a_refused_header_frees_the_map);

    RUN_TEST(test_insert_then_contains);
    RUN_TEST(test_contains_on_an_empty_set_is_false);
    RUN_TEST(test_inserting_a_key_twice_keeps_one_entry);
    RUN_TEST(test_insert_reports_whether_the_key_was_new);
    RUN_TEST(test_find_gives_the_key_back);
    RUN_TEST(test_a_set_whose_keys_all_collide_still_finds_them);
    RUN_TEST(test_wide_keys_travel_whole);

    RUN_TEST(test_every_key_survives_the_growths);
    RUN_TEST(test_a_borrowed_node_survives_every_growth);
    RUN_TEST(test_reserve_grows_the_buckets_only);
    RUN_TEST(test_shrink_to_fit_gives_the_buckets_back);
    RUN_TEST(test_shrink_to_fit_of_an_empty_set_owns_nothing);
    RUN_TEST(test_shrink_to_fit_keeps_borrowed_nodes);

    RUN_TEST(test_remove_takes_the_key_out);
    RUN_TEST(test_remove_of_a_missing_key_says_so);
    RUN_TEST(test_remove_from_the_middle_of_a_chain);
    RUN_TEST(test_mut_walk_removes_through_the_nodes);
    RUN_TEST(test_mut_walk_of_an_empty_set_stops_at_once);
    RUN_TEST(test_remove_node_drops_the_key_it_names);
    RUN_TEST(test_a_key_can_be_put_back_after_removal);
    RUN_TEST(test_clear_empties_and_keeps_the_buckets);
    RUN_TEST(test_clear_leaves_a_usable_set);

    RUN_TEST(test_the_walk_reaches_every_key_once);
    RUN_TEST(test_the_walk_crosses_a_single_chain);
    RUN_TEST(test_the_walk_after_a_removal_sees_the_rest);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_carries_the_hasher_and_the_allocator);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_empties_the_source);
    RUN_TEST(test_move_assign_across_allocators_reports_an_exhausted_arena);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_a_copy_keeps_the_smaller_node);
    RUN_TEST(test_copy_of_empty_stays_empty);
    RUN_TEST(test_copy_assign_overwrites_the_target);
    RUN_TEST(test_copy_assign_self_is_noop);

    RUN_TEST(test_swap_exchanges_the_keys_and_the_hashers);
    RUN_TEST(test_swap_keeps_the_nodes_alive);
    RUN_TEST(test_swap_self_is_noop);

    RUN_TEST(test_new_reports_an_exhausted_arena);
    RUN_TEST(test_insert_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_a_repeat_insert_needs_no_allocator);
    RUN_TEST(test_reserve_reports_an_exhausted_arena);
    RUN_TEST(test_copy_reports_an_exhausted_arena);
    RUN_TEST(test_copy_assign_leaves_the_target_untouched_on_failure);


    RUN_TEST(test_eq_ignores_insertion_order_and_bucket_count);
    RUN_TEST(test_eq_looks_the_keys_up_with_the_hasher_of_the_other);
    RUN_TEST(test_eq_parts_a_differing_key);
    RUN_TEST(test_eq_parts_different_lengths);
    RUN_TEST(test_eq_of_two_empties);
    RUN_TEST(test_eq_walks_whole_chains);
    RUN_TEST(test_eq_matches_a_copy);

    RUN_TEST(test_fprint_writes_the_key);
    RUN_TEST(test_fprint_of_an_empty_set);
    RUN_TEST(test_print_writes_to_stdout);

    return UNITY_END();
}
