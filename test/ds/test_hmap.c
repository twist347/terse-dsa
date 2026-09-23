#include "tda/ds/hmap.h"
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

// every key in one bucket. Without it the chains stay one node long and the code that
// walks them is never reached, whatever the keys are
static tda_Hash hash_all_alike(const void *x) {
    TDA_UNUSED(x);

    return 0;
}

// two keys per bucket at most, whatever the map's size: enough to build chains without
// collapsing the whole map into one
static tda_Hash hash_by_parity(const void *x) {
    return (tda_Hash) (*(const int32_t *) x & 1);
}

// a hasher and an equality for Pair, built the way core/hash prescribes: a struct's hash
// is folded from its fields' rather than taken over its bytes, since padding is not part
// of the value
static tda_Hash hash_pair(const void *x) {
    const Pair *p = x;

    return tda_hash_combine(tda_hash_i64(&p->a), tda_hash_i64(&p->b));
}

static bool eq_pair(const void *lhs, const void *rhs) {
    const Pair *a = lhs;
    const Pair *b = rhs;

    return a->a == b->a && a->b == b->b;
}

// hashes that differ in the high bits and agree in the low ones: every key lands in one
// bucket while no two hashes are equal
static tda_Hash hash_high_bits_only(const void *x) {
    return ((tda_Hash) *(const int32_t *) x) << 32;
}

static size_t hash_calls = 0;

static tda_Hash hash_i32_counting(const void *x) {
    ++hash_calls;

    return tda_hash_i32(x);
}

static size_t eq_calls = 0;

static bool eq_i32_counting(const void *lhs, const void *rhs) {
    ++eq_calls;

    return tda_eq_i32(lhs, rhs);
}

[[nodiscard]]
static tda_HMap *make_map(tda_Hasher hasher) {
    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, hasher, tda_eq_i32, tda_al_default(), &m));

    return m;
}

static void put(tda_HMap *m, int32_t key, int32_t val) {
    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, m, key, val, nullptr));
}

// 'key * 10' under every key from 0 to n - 1
[[nodiscard]]
static tda_HMap *make_filled(tda_Hasher hasher, int32_t n) {
    tda_HMap *m = make_map(hasher);
    for (int32_t i = 0; i < n; ++i) {
        put(m, i, i * 10);
    }
    return m;
}

static void assert_has(const tda_HMap *m, int32_t key, int32_t val) {
    const int32_t *got = TDA_HMAP_GET_AS(int32_t, int32_t, m, key);
    TEST_ASSERT_NOT_NULL_MESSAGE(got, "the key is missing");
    TEST_ASSERT_EQUAL_INT32(val, *got);
    TEST_ASSERT_TRUE(TDA_HMAP_CONTAINS(int32_t, m, key));
}

static void assert_missing(const tda_HMap *m, int32_t key) {
    TEST_ASSERT_NULL(TDA_HMAP_GET_AS(int32_t, int32_t, m, key));
    TEST_ASSERT_FALSE(TDA_HMAP_CONTAINS(int32_t, m, key));
    TEST_ASSERT_NULL(TDA_HMAP_FIND(int32_t, m, key));
}

// the walk reaches exactly 'len' entries, and every one of them answers get with the
// value it carries. The order is unspecified, so that is all a walk can promise
static void assert_walk_sees_everything(const tda_HMap *m) {
    size_t seen = 0;
    TDA_HMAP_FOR_EACH(node, m) {
        const int32_t key = *TDA_HMAP_NODE_KEY_AS(int32_t, node);
        const int32_t val = *TDA_HMAP_NODE_VAL_AS(int32_t, m, node);
        assert_has(m, key, val);
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(tda_hmap_len(m), seen);
}

/* ========== lifetime ========== */

static void test_new_starts_empty() {
    tda_HMap *m = make_map(tda_hash_i32);

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(m));
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_bucket_count(m));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_hmap_key_size(m));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_hmap_val_size(m));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_hmap_al(m));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hmap_hasher(m));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hmap_key_eq(m));
    TEST_ASSERT_NULL(tda_hmap_first_node(m));

    tda_hmap_drop(m);
}

// an empty map owns no buckets at all: the array is taken on the first insert
static void test_new_takes_no_buckets_until_the_first_insert() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &m));
    TEST_ASSERT_EQUAL_size_t(1, probe.live); // the header alone

    put(m, 1, 10);
    TEST_ASSERT_EQUAL_size_t(3, probe.live); // header, buckets, one node

    tda_hmap_drop(m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_new_cap_reserves_buckets_without_entries() {
    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW_CAP(int32_t, int32_t, 100, tda_hash_i32, tda_eq_i32, tda_al_default(), &m));

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(m));
    TEST_ASSERT_TRUE(tda_hmap_bucket_count(m) >= 100);

    tda_hmap_drop(m);
}

static void test_drop_null_is_noop() {
    tda_hmap_drop(nullptr);
}

static void test_drop_hands_back_every_node() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &m));
    for (int32_t i = 0; i < 20; ++i) {
        put(m, i, i);
    }

    tda_hmap_drop(m);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== insert and lookup ========== */

static void test_insert_then_get() {
    tda_HMap *m = make_map(tda_hash_i32);

    put(m, 1, 10);
    put(m, 2, 20);

    assert_has(m, 1, 10);
    assert_has(m, 2, 20);
    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(m));

    tda_hmap_drop(m);
}

static void test_get_of_a_missing_key_is_null() {
    tda_HMap *m = make_map(tda_hash_i32);
    put(m, 1, 10);

    assert_missing(m, 2);
    assert_missing(m, -1);

    tda_hmap_drop(m);
}

static void test_get_on_an_empty_map_is_null() {
    tda_HMap *m = make_map(tda_hash_i32);

    assert_missing(m, 1);
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(m));

    tda_hmap_drop(m);
}

static void test_insert_overwrites_an_existing_key() {
    tda_HMap *m = make_map(tda_hash_i32);

    put(m, 1, 10);
    put(m, 1, 99);

    assert_has(m, 1, 99);
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(m)); // an overwrite is not a second entry

    tda_hmap_drop(m);
}

static void test_insert_reports_whether_the_key_was_new() {
    tda_HMap *m = make_map(tda_hash_i32);

    bool is_new = false;
    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, m, 1, 10, &is_new));
    TEST_ASSERT_TRUE(is_new);

    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, m, 1, 20, &is_new));
    TEST_ASSERT_FALSE(is_new);

    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, m, 2, 20, &is_new));
    TEST_ASSERT_TRUE(is_new);

    tda_hmap_drop(m);
}

static void test_get_mut_writes_through() {
    tda_HMap *m = make_map(tda_hash_i32);
    put(m, 1, 10);

    *TDA_HMAP_GET_MUT_AS(int32_t, int32_t, m, 1) = 42;

    assert_has(m, 1, 42);

    tda_hmap_drop(m);
}

static void test_find_gives_the_entry_as_a_node() {
    tda_HMap *m = make_map(tda_hash_i32);
    put(m, 7, 70);

    const tda_HMapNode *node = TDA_HMAP_FIND(int32_t, m, 7);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT32(7, *TDA_HMAP_NODE_KEY_AS(int32_t, node));
    TEST_ASSERT_EQUAL_INT32(70, *TDA_HMAP_NODE_VAL_AS(int32_t, m, node));

    tda_hmap_drop(m);
}

static void test_node_val_mut_writes_through() {
    tda_HMap *m = make_map(tda_hash_i32);
    put(m, 7, 70);

    tda_HMapNode *node = TDA_HMAP_FIND_MUT(int32_t, m, 7);
    *TDA_HMAP_NODE_VAL_MUT_AS(int32_t, m, node) = 77;

    assert_has(m, 7, 77);

    tda_hmap_drop(m);
}

// every key lands in one bucket, so lookup has to walk a chain instead of hitting the
// head of it
static void test_a_map_whose_keys_all_collide_still_finds_them() {
    tda_HMap *m = make_filled(hash_all_alike, 20);

    TEST_ASSERT_EQUAL_size_t(20, tda_hmap_len(m));
    for (int32_t i = 0; i < 20; ++i) {
        assert_has(m, i, i * 10);
    }
    assert_missing(m, 20);

    tda_hmap_drop(m);
}

static void test_colliding_keys_overwrite_only_their_own_entry() {
    tda_HMap *m = make_filled(hash_all_alike, 5);

    put(m, 3, 999);

    TEST_ASSERT_EQUAL_size_t(5, tda_hmap_len(m));
    assert_has(m, 0, 0);
    assert_has(m, 3, 999);
    assert_has(m, 4, 40);

    tda_hmap_drop(m);
}

// a hash that agrees where the keys do not: 'eq' is what has the final word
static void test_a_hash_collision_is_not_an_equality() {
    tda_HMap *m = make_map(hash_all_alike);

    put(m, 1, 10);
    put(m, 2, 20);

    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(m));
    assert_has(m, 1, 10);
    assert_has(m, 2, 20);

    tda_hmap_drop(m);
}

static void test_wide_keys_and_values_travel_whole() {
    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(Pair, Pair, hash_pair, eq_pair, tda_al_default(), &m));

    constexpr Pair key = {1, 2};
    constexpr Pair val = {30, 40};
    TDA_TEST_OK(tda_hmap_insert(m, &key, &val, nullptr));

    const Pair *got = tda_hmap_get(m, &key);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_INT64(30, got->a);
    TEST_ASSERT_EQUAL_INT64(40, got->b);
    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_hmap_key_size(m));

    tda_hmap_drop(m);
}

// what the eight bytes of cached hash buy: walking a chain of ten, only the node whose
// hash matches is ever handed to 'eq', and a miss asks it nothing at all
static void test_the_chain_is_walked_by_hash_before_eq() {
    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, hash_high_bits_only, eq_i32_counting, tda_al_default(), &m));

    for (int32_t i = 0; i < 10; ++i) {
        put(m, i, i * 10);
    }
    eq_calls = 0;
    const int32_t *got = TDA_HMAP_GET_AS(int32_t, int32_t, m, 7);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_INT32(70, *got);
    TEST_ASSERT_EQUAL_size_t(1, eq_calls);

    eq_calls = 0;
    assert_missing(m, 100);
    TEST_ASSERT_EQUAL_size_t(0, eq_calls);

    tda_hmap_drop(m);
}

/* ========== growth ========== */

static void test_the_buckets_double_as_the_map_fills() {
    tda_HMap *m = make_map(tda_hash_i32);

    put(m, 0, 0);
    const size_t first = tda_hmap_bucket_count(m);
    TEST_ASSERT_TRUE(first > 0);

    for (int32_t i = 1; i < 200; ++i) {
        put(m, i, i);
    }

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(m) > first);
    TEST_ASSERT_TRUE(tda_hmap_len(m) <= tda_hmap_bucket_count(m));

    tda_hmap_drop(m);
}

static void test_every_entry_survives_the_growths() {
    tda_HMap *m = make_filled(tda_hash_i32, 300);

    TEST_ASSERT_EQUAL_size_t(300, tda_hmap_len(m));
    for (int32_t i = 0; i < 300; ++i) {
        assert_has(m, i, i * 10);
    }
    assert_missing(m, 300);

    tda_hmap_drop(m);
}

// the property chaining was chosen for: growth reallocates the bucket array and relinks,
// so the node a caller borrowed before it is still that entry afterwards
static void test_a_borrowed_node_survives_every_growth() {
    tda_HMap *m = make_map(tda_hash_i32);

    for (int32_t i = 0; i < 4; ++i) {
        put(m, i, i * 10);
    }

    const tda_HMapNode *held[4];
    for (int32_t i = 0; i < 4; ++i) {
        held[i] = TDA_HMAP_FIND(int32_t, m, i);
        TEST_ASSERT_NOT_NULL(held[i]);
    }

    for (int32_t i = 4; i < 500; ++i) {
        put(m, i, i * 10);
    }

    for (int32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_PTR(held[i], TDA_HMAP_FIND(int32_t, m, i));
        TEST_ASSERT_EQUAL_INT32(i, *TDA_HMAP_NODE_KEY_AS(int32_t, held[i]));
        TEST_ASSERT_EQUAL_INT32(i * 10, *TDA_HMAP_NODE_VAL_AS(int32_t, m, held[i]));
    }

    tda_hmap_drop(m);
}

// growing moves no entry, so the only allocation a rehash makes is the bucket array
static void test_a_growth_allocates_only_the_bucket_array() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW_CAP(int32_t, int32_t, 8, tda_hash_i32, tda_eq_i32, &al, &m));
    for (int32_t i = 0; i < 8; ++i) {
        put(m, i, i);
    }

    const size_t before = tda_test_probe_requests(&probe);
    put(m, 8, 8); // the one that pushes the load past one per bucket

    // one array plus one node, and not a single request per entry moved
    TEST_ASSERT_EQUAL_size_t(2, tda_test_probe_requests(&probe) - before);

    tda_hmap_drop(m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_reserve_grows_the_buckets_only() {
    tda_HMap *m = make_filled(tda_hash_i32, 4);

    TDA_TEST_OK(tda_hmap_reserve(m, 1000));

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(m) >= 1000);
    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(m));
    for (int32_t i = 0; i < 4; ++i) {
        assert_has(m, i, i * 10);
    }

    tda_hmap_drop(m);
}

static void test_reserve_below_the_bucket_count_changes_nothing() {
    tda_HMap *m = make_filled(tda_hash_i32, 20);
    const size_t before = tda_hmap_bucket_count(m);

    TDA_TEST_OK(tda_hmap_reserve(m, 1));

    TEST_ASSERT_EQUAL_size_t(before, tda_hmap_bucket_count(m));

    tda_hmap_drop(m);
}

/* ========== remove ========== */

static void test_remove_takes_the_entry_out() {
    tda_HMap *m = make_filled(tda_hash_i32, 5);

    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, 2));

    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(m));
    assert_missing(m, 2);
    assert_has(m, 1, 10);
    assert_has(m, 3, 30);

    tda_hmap_drop(m);
}

static void test_remove_of_a_missing_key_says_so() {
    tda_HMap *m = make_filled(tda_hash_i32, 3);

    TEST_ASSERT_FALSE(TDA_HMAP_REMOVE(int32_t, m, 99));
    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(m));

    tda_hmap_drop(m);
}

static void test_remove_on_an_empty_map_says_so() {
    tda_HMap *m = make_map(tda_hash_i32);

    TEST_ASSERT_FALSE(TDA_HMAP_REMOVE(int32_t, m, 1));

    tda_hmap_drop(m);
}

// unlinking from the middle of a chain is the case the head-of-bucket path never reaches
static void test_remove_from_the_middle_of_a_chain() {
    tda_HMap *m = make_filled(hash_all_alike, 5);

    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, 2));

    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(m));
    assert_missing(m, 2);
    for (int32_t i = 0; i < 5; ++i) {
        if (i != 2) {
            assert_has(m, i, i * 10);
        }
    }

    tda_hmap_drop(m);
}

static void test_remove_every_key_of_a_chain_in_turn() {
    tda_HMap *m = make_filled(hash_all_alike, 6);

    for (int32_t i = 0; i < 6; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, i));
        TEST_ASSERT_EQUAL_size_t((size_t) (5 - i), tda_hmap_len(m));
    }

    TEST_ASSERT_NULL(tda_hmap_first_node(m));

    tda_hmap_drop(m);
}

// the mutable walk is the const one with a value to write through: every entry doubled
// in one pass, without a lookup per key
static void test_mut_walk_writes_through_every_entry() {
    tda_HMap *m = make_filled(tda_hash_i32, 4);

    size_t seen = 0;
    for (tda_HMapNode *node = tda_hmap_first_node_mut(m); node; node = tda_hmap_node_next_mut(m, node)) {
        *TDA_HMAP_NODE_VAL_MUT_AS(int32_t, m, node) *= 2;
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(4, seen);

    for (int32_t key = 0; key < 4; ++key) {
        TEST_ASSERT_EQUAL_INT32(key * 20, *TDA_HMAP_GET_AS(int32_t, int32_t, m, key));
    }

    tda_hmap_drop(m);
}

static void test_mut_walk_of_an_empty_map_stops_at_once() {
    tda_HMap *m = make_map(tda_hash_i32);

    TEST_ASSERT_NULL(tda_hmap_first_node_mut(m));

    tda_hmap_drop(m);
}

static void test_remove_node_drops_the_entry_it_names() {
    tda_HMap *m = make_filled(hash_all_alike, 4);

    tda_HMapNode *node = tda_hmap_find_mut(m, &(int32_t){2});
    TEST_ASSERT_NOT_NULL(node);

    tda_hmap_remove_node(m, node);

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(m));
    assert_missing(m, 2);
    assert_has(m, 3, 30);

    tda_hmap_drop(m);
}

static void test_a_key_can_be_put_back_after_removal() {
    tda_HMap *m = make_filled(tda_hash_i32, 3);

    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, 1));
    put(m, 1, 111);

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(m));
    assert_has(m, 1, 111);

    tda_hmap_drop(m);
}

static void test_clear_empties_and_keeps_the_buckets() {
    tda_HMap *m = make_filled(tda_hash_i32, 20);
    const size_t buckets = tda_hmap_bucket_count(m);

    tda_hmap_clear(m);

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(m));
    TEST_ASSERT_EQUAL_size_t(buckets, tda_hmap_bucket_count(m));
    TEST_ASSERT_NULL(tda_hmap_first_node(m));
    assert_missing(m, 1);

    tda_hmap_drop(m);
}

static void test_clear_leaves_a_usable_map() {
    tda_HMap *m = make_filled(tda_hash_i32, 10);

    tda_hmap_clear(m);
    put(m, 5, 55);

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(m));
    assert_has(m, 5, 55);

    tda_hmap_drop(m);
}

/* ========== walk ========== */

static void test_the_walk_reaches_every_entry_once() {
    tda_HMap *m = make_filled(tda_hash_i32, 50);

    assert_walk_sees_everything(m);

    tda_hmap_drop(m);
}

// one bucket holds everything, so the walk is one chain and never has to skip
static void test_the_walk_crosses_a_single_chain() {
    tda_HMap *m = make_filled(hash_all_alike, 12);

    assert_walk_sees_everything(m);

    tda_hmap_drop(m);
}

// two full buckets among many empty ones: the walk has to step over the gaps
static void test_the_walk_skips_the_empty_buckets() {
    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW_CAP(int32_t, int32_t, 64, hash_by_parity, tda_eq_i32, tda_al_default(), &m));
    for (int32_t i = 0; i < 10; ++i) {
        put(m, i, i * 10);
    }

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(m) >= 64);
    assert_walk_sees_everything(m);

    tda_hmap_drop(m);
}

static void test_the_walk_of_an_empty_map_stops_at_once() {
    tda_HMap *m = make_map(tda_hash_i32);
    TEST_ASSERT_NULL(tda_hmap_first_node(m));

    tda_HMap *cleared = make_filled(tda_hash_i32, 5);
    tda_hmap_clear(cleared);
    TEST_ASSERT_NULL(tda_hmap_first_node(cleared));

    tda_hmap_drop(m);
    tda_hmap_drop(cleared);
}

static void test_the_walk_after_a_removal_sees_the_rest() {
    tda_HMap *m = make_filled(tda_hash_i32, 30);

    for (int32_t i = 0; i < 30; i += 2) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, i));
    }

    TEST_ASSERT_EQUAL_size_t(15, tda_hmap_len(m));
    assert_walk_sees_everything(m);

    tda_hmap_drop(m);
}

/* ========== get_or_insert ========== */

static void test_get_or_insert_puts_a_missing_key_in() {
    tda_HMap *m = make_map(tda_hash_i32);

    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, m, 1, 10, &node));

    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT32(1, *TDA_HMAP_NODE_KEY_AS(int32_t, node));
    TEST_ASSERT_EQUAL_INT32(10, *TDA_HMAP_NODE_VAL_AS(int32_t, m, node));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(m));
    assert_has(m, 1, 10);

    tda_hmap_drop(m);
}

// unlike insert, it does NOT overwrite: the value it carries is only for a key that was
// not there
static void test_get_or_insert_leaves_a_present_key_alone() {
    tda_HMap *m = make_map(tda_hash_i32);
    put(m, 1, 10);

    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, m, 1, 999, &node));

    TEST_ASSERT_EQUAL_INT32(10, *TDA_HMAP_NODE_VAL_AS(int32_t, m, node));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(m));

    tda_hmap_drop(m);
}

static void test_get_or_insert_hands_back_the_node_that_is_already_there() {
    tda_HMap *m = make_filled(hash_all_alike, 5);
    const tda_HMapNode *held = TDA_HMAP_FIND(int32_t, m, 3);

    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, m, 3, 0, &node));

    TEST_ASSERT_EQUAL_PTR(held, node);

    tda_hmap_drop(m);
}

// the whole reason the operation exists. Reaching the same result through get_mut and
// then insert asks the hasher twice whenever the key turns out to be absent
static void test_get_or_insert_hashes_the_key_once() {
    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, hash_i32_counting, tda_eq_i32, tda_al_default(), &m));

    hash_calls = 0;
    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, m, 1, 10, &node));
    TEST_ASSERT_EQUAL_size_t(1, hash_calls);

    // the pair it replaces, on a key that is not there either
    hash_calls = 0;
    if (!tda_hmap_get_mut(m, &(int32_t){2})) {
        TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, m, 2, 20, nullptr));
    }
    TEST_ASSERT_EQUAL_size_t(2, hash_calls);

    // and once again on a key that IS there, where both forms cost the same
    hash_calls = 0;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, m, 1, 0, &node));
    TEST_ASSERT_EQUAL_size_t(1, hash_calls);

    tda_hmap_drop(m);
}

// the idiom it was added for: a tally that never looks a key up twice
static void test_get_or_insert_carries_the_counter_idiom() {
    tda_HMap *m = make_map(tda_hash_i32);

    constexpr int32_t seen[6] = {3, 1, 3, 3, 1, 7};
    for (size_t i = 0; i < 6; ++i) {
        tda_HMapNode *node = nullptr;
        TDA_TEST_OK(tda_hmap_get_or_insert(m, &seen[i], &(int32_t){ 0 }, &node));
        ++*TDA_HMAP_NODE_VAL_MUT_AS(int32_t, m, node);
    }

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(m));
    assert_has(m, 3, 3);
    assert_has(m, 1, 2);
    assert_has(m, 7, 1);

    tda_hmap_drop(m);
}

/* ========== shrink_to_fit ========== */

static void test_shrink_to_fit_gives_the_buckets_back() {
    tda_HMap *m = make_filled(tda_hash_i32, 300);
    const size_t grown = tda_hmap_bucket_count(m);

    for (int32_t i = 3; i < 300; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, i));
    }

    TDA_TEST_OK(tda_hmap_shrink_to_fit(m));

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(m) < grown);
    TEST_ASSERT_TRUE(tda_hmap_bucket_count(m) >= tda_hmap_len(m));
    for (int32_t i = 0; i < 3; ++i) {
        assert_has(m, i, i * 10);
    }
    assert_walk_sees_everything(m);

    tda_hmap_drop(m);
}

// the floor is the count a fresh map takes: a nearly empty map lands exactly where a new
// one starts, not at some denser shape that happens to still work
static void test_shrink_to_fit_stops_at_the_starting_size() {
    tda_HMap *fresh = make_map(tda_hash_i32);
    put(fresh, 0, 0);
    const size_t base = tda_hmap_bucket_count(fresh);

    tda_HMap *m = make_filled(tda_hash_i32, 300);
    for (int32_t i = 1; i < 300; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, i));
    }

    TDA_TEST_OK(tda_hmap_shrink_to_fit(m));

    TEST_ASSERT_EQUAL_size_t(base, tda_hmap_bucket_count(m));
    assert_has(m, 0, 0);

    tda_hmap_drop(fresh);
    tda_hmap_drop(m);
}

// nothing left to hold, so the map owns nothing at all — as it did when it was made
static void test_shrink_to_fit_of_an_empty_map_owns_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &m));
    for (int32_t i = 0; i < 50; ++i) {
        put(m, i, i);
    }
    tda_hmap_clear(m);

    TDA_TEST_OK(tda_hmap_shrink_to_fit(m));

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_bucket_count(m));
    TEST_ASSERT_EQUAL_size_t(1, probe.live); // the header alone
    TEST_ASSERT_NULL(tda_hmap_first_node(m));

    // and it is still a map: the next insert takes buckets again
    put(m, 1, 10);
    assert_has(m, 1, 10);

    tda_hmap_drop(m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// shrinking relinks the entries where they lie, exactly as growing does, so a borrowed
// node comes through it
static void test_shrink_to_fit_keeps_borrowed_nodes() {
    tda_HMap *m = make_filled(tda_hash_i32, 200);

    const tda_HMapNode *held[3];
    for (int32_t i = 0; i < 3; ++i) {
        held[i] = TDA_HMAP_FIND(int32_t, m, i);
    }

    for (int32_t i = 3; i < 200; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, i));
    }
    TDA_TEST_OK(tda_hmap_shrink_to_fit(m));

    for (int32_t i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_PTR(held[i], TDA_HMAP_FIND(int32_t, m, i));
        TEST_ASSERT_EQUAL_INT32(i * 10, *TDA_HMAP_NODE_VAL_AS(int32_t, m, held[i]));
    }

    tda_hmap_drop(m);
}

static void test_shrink_to_fit_allocates_only_the_bucket_array() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &m));
    for (int32_t i = 0; i < 100; ++i) {
        put(m, i, i);
    }
    for (int32_t i = 2; i < 100; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, i));
    }

    const size_t before = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_hmap_shrink_to_fit(m));

    TEST_ASSERT_EQUAL_size_t(1, tda_test_probe_requests(&probe) - before);

    tda_hmap_drop(m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a map that is already as small as it can be asks the allocator for nothing
static void test_shrink_to_fit_when_already_tight_is_a_noop() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &m));
    put(m, 1, 10);

    TDA_TEST_OK(tda_hmap_shrink_to_fit(m));
    const size_t buckets = tda_hmap_bucket_count(m);
    const size_t before = tda_test_probe_requests(&probe);

    TDA_TEST_OK(tda_hmap_shrink_to_fit(m));

    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_size_t(buckets, tda_hmap_bucket_count(m));

    tda_hmap_drop(m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_a_shrunk_map_grows_again() {
    tda_HMap *m = make_filled(tda_hash_i32, 100);
    for (int32_t i = 2; i < 100; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, m, i));
    }
    TDA_TEST_OK(tda_hmap_shrink_to_fit(m));

    for (int32_t i = 2; i < 100; ++i) {
        put(m, i, i * 10);
    }

    TEST_ASSERT_EQUAL_size_t(100, tda_hmap_len(m));
    for (int32_t i = 0; i < 100; ++i) {
        assert_has(m, i, i * 10);
    }

    tda_hmap_drop(m);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_HMap *src = make_filled(tda_hash_i32, 10);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(tda_hmap_copy(src, &dst));

    put(dst, 0, 999);
    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, dst, 1));

    assert_has(src, 0, 0);
    assert_has(src, 1, 10);
    assert_has(dst, 0, 999);
    assert_missing(dst, 1);

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
}

static void test_copy_carries_the_hasher_and_the_equality() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *src = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, hash_all_alike, tda_eq_i32, arena, &src));
    put(src, 1, 10);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(tda_hmap_copy(src, &dst));

    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(dst));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hmap_key_eq(dst));
    TEST_ASSERT_EQUAL_PTR(arena, tda_hmap_al(dst));
    assert_has(dst, 1, 10);

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *src = make_filled(tda_hash_i32, 8);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(tda_hmap_copy_with(src, arena, &dst));

    TEST_ASSERT_EQUAL_PTR(arena, tda_hmap_al(dst));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_hmap_al(src));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hmap_hasher(dst));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hmap_key_eq(dst));
    TEST_ASSERT_TRUE(tda_hmap_eq(src, dst));

    // the source is gone and the copy still answers: the nodes are its own
    tda_hmap_drop(src);
    assert_has(dst, 3, 30);

    tda_hmap_drop(dst);
    tda_al_arena_drop(arena);
}

// the buckets and the nodes are asked of the allocator the copy is going to
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    tda_HMap *src = make_filled(tda_hash_i32, 8);

    tda_HMap *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_copy_with(src, arena, &dst));
    TEST_ASSERT_NULL(dst);
    TEST_ASSERT_EQUAL_size_t(8, tda_hmap_len(src));

    tda_hmap_drop(src);
    tda_al_arena_drop(arena);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *src = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, hash_all_alike, tda_eq_i32, &al, &src));
    put(src, 1, 10);
    put(src, 2, 20);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &dst));
    put(dst, 9, 90);

    const size_t requests = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_hmap_move_assign(dst, src));

    // nothing was asked of the allocator: the buckets and the nodes changed hands
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(dst));
    assert_has(dst, 1, 10);
    assert_has(dst, 2, 20);

    // the hasher travels with the entries, or the table would be read under another order
    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(dst));

    // the source gives back everything, its buckets included, as every container does
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(src));
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_bucket_count(src));

    // and is still a map: the first insert makes its buckets anew
    put(src, 5, 50);
    assert_has(src, 5, 50);

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_move_assign_across_allocators_empties_the_source() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *src = make_filled(tda_hash_i32, 8);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, hash_all_alike, tda_eq_i32, arena, &dst));
    put(dst, 9, 90);

    TDA_TEST_OK(tda_hmap_move_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(8, tda_hmap_len(dst));
    assert_has(dst, 3, 30);
    assert_missing(dst, 9);
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hmap_hasher(dst));
    TEST_ASSERT_EQUAL_PTR(arena, tda_hmap_al(dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(src));
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_bucket_count(src));
    TEST_ASSERT_EQUAL_PTR(tda_al_default(), tda_hmap_al(src));

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_across_allocators_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &dst));
    put(dst, 9, 90);
    tda_test_arena_leave(arena, 0);

    tda_HMap *src = make_filled(tda_hash_i32, 8);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_move_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(8, tda_hmap_len(src));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(dst));
    assert_has(dst, 9, 90);

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
    tda_al_arena_drop(arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_HMap *m = make_filled(tda_hash_i32, 4);

    TDA_TEST_OK(tda_hmap_move_assign(m, m));

    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(m));
    assert_has(m, 2, 20);

    tda_hmap_drop(m);
}

static void test_copy_of_empty_stays_empty() {
    tda_HMap *src = make_map(tda_hash_i32);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(tda_hmap_copy(src, &dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(dst));

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
}

static void test_copy_assign_overwrites_the_target() {
    tda_HMap *src = make_filled(tda_hash_i32, 6);
    tda_HMap *dst = make_filled(tda_hash_i32, 2);
    put(dst, 100, 100);

    TDA_TEST_OK(tda_hmap_copy_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(6, tda_hmap_len(dst));
    assert_missing(dst, 100);
    for (int32_t i = 0; i < 6; ++i) {
        assert_has(dst, i, i * 10);
    }

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
}

static void test_copy_assign_hands_over_the_hasher_too() {
    tda_HMap *src = make_map(hash_all_alike);
    put(src, 1, 10);

    tda_HMap *dst = make_map(tda_hash_i32);
    put(dst, 2, 20);

    TDA_TEST_OK(tda_hmap_copy_assign(dst, src));

    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(dst));
    assert_has(dst, 1, 10);
    assert_missing(dst, 2);

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
}

static void test_copy_assign_self_is_noop() {
    tda_HMap *m = make_filled(tda_hash_i32, 5);

    TDA_TEST_OK(tda_hmap_copy_assign(m, m));

    TEST_ASSERT_EQUAL_size_t(5, tda_hmap_len(m));
    assert_has(m, 3, 30);

    tda_hmap_drop(m);
}

static void test_copy_assign_keeps_the_target_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *src = make_filled(tda_hash_i32, 4);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &dst));

    TDA_TEST_OK(tda_hmap_copy_assign(dst, src));

    TEST_ASSERT_EQUAL_PTR(arena, tda_hmap_al(dst));
    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(dst));

    tda_hmap_drop(src);
    tda_hmap_drop(dst);
    tda_al_arena_drop(arena);
}

// what the target held before must go back to the allocator rather than be stranded when
// the clone takes its place. The default allocator would say nothing about it
static void test_copy_assign_hands_back_the_old_contents() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &dst));
    for (int32_t i = 0; i < 20; ++i) {
        put(dst, i, i);
    }

    tda_HMap *src = make_filled(tda_hash_i32, 3);
    TDA_TEST_OK(tda_hmap_copy_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(dst));

    tda_hmap_drop(src);
    tda_hmap_drop(dst);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== swap ========== */

static void test_swap_exchanges_the_entries_and_the_hashers() {
    tda_HMap *a = make_map(tda_hash_i32);
    put(a, 1, 10);

    tda_HMap *b = make_map(hash_all_alike);
    put(b, 2, 20);
    put(b, 3, 30);

    tda_hmap_swap(a, b);

    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(a));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(b));
    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(a));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hmap_hasher(b));
    assert_has(a, 2, 20);
    assert_has(b, 1, 10);

    tda_hmap_drop(a);
    tda_hmap_drop(b);
}

// the nodes change map without moving, which is why swap wants one allocator
static void test_swap_keeps_the_nodes_alive() {
    tda_HMap *a = make_filled(tda_hash_i32, 4);
    tda_HMap *b = make_map(tda_hash_i32);

    const tda_HMapNode *held = TDA_HMAP_FIND(int32_t, a, 2);
    TEST_ASSERT_NOT_NULL(held);

    tda_hmap_swap(a, b);

    TEST_ASSERT_EQUAL_PTR(held, TDA_HMAP_FIND(int32_t, b, 2));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
}

static void test_swap_self_is_noop() {
    tda_HMap *m = make_filled(tda_hash_i32, 3);

    tda_hmap_swap(m, m);

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(m));
    assert_has(m, 1, 10);

    tda_hmap_drop(m);
}

/* ========== allocation failure ========== */

static void test_new_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 64);
    TEST_ASSERT_NOT_NULL(arena);
    tda_test_arena_leave(arena, 0);

    tda_HMap *m = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &m));
    TEST_ASSERT_NULL(m);

    tda_al_arena_drop(arena);
}

// the header is taken and the bucket array is refused: the header must not be stranded
static void test_new_cap_frees_the_header_when_the_buckets_are_refused() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 1);

    tda_HMap *m = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HMAP_NEW_CAP(int32_t, int32_t, 16, tda_hash_i32, tda_eq_i32, &al, &m));

    TEST_ASSERT_NULL(m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_insert_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &m));
    put(m, 1, 10);
    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HMAP_INSERT(int32_t, int32_t, m, 2, 20, nullptr));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(m));
    assert_has(m, 1, 10);
    assert_missing(m, 2);

    tda_al_arena_drop(arena);
}

// overwriting an existing key needs no memory at all, so an exhausted arena is no
// obstacle to it
static void test_an_overwrite_needs_no_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &m));
    put(m, 1, 10);
    tda_test_arena_leave(arena, 0);

    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, m, 1, 99, nullptr));
    assert_has(m, 1, 99);

    tda_al_arena_drop(arena);
}

static void test_reserve_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &m));
    put(m, 1, 10);
    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_reserve(m, 100000));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(m));
    assert_has(m, 1, 10);

    tda_al_arena_drop(arena);
}

static void test_copy_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *src = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &src));
    put(src, 1, 10);
    tda_test_arena_leave(arena, 0);

    tda_HMap *dst = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_copy(src, &dst));
    TEST_ASSERT_NULL(dst);

    tda_al_arena_drop(arena);
}

// a refused clone must leave the target whole, not half overwritten
static void test_copy_assign_leaves_the_target_untouched_on_failure() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *src = make_filled(tda_hash_i32, 50);

    tda_HMap *dst = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &dst));
    put(dst, 7, 70);
    tda_test_arena_leave(arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_copy_assign(dst, src));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(dst));
    assert_has(dst, 7, 70);

    tda_hmap_drop(src);
    tda_al_arena_drop(arena);
}

// the node is taken and then the map is left as it was: nothing leaks on the way out
static void test_a_refused_node_leaves_nothing_behind() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, &al, &m));
    put(m, 1, 10);

    tda_test_probe_fail_after_next(&probe, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HMAP_INSERT(int32_t, int32_t, m, 2, 20, nullptr));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(m));
    assert_has(m, 1, 10);

    tda_test_probe_reset(&probe);
    tda_hmap_drop(m);
}

static void test_get_or_insert_reports_an_exhausted_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &m));
    put(m, 1, 10);
    tda_test_arena_leave(arena, 0);

    tda_HMapNode *node = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, m, 2, 20, &node));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(m));
    assert_missing(m, 2);

    tda_al_arena_drop(arena);
}

// finding a key that is there needs no memory, so an exhausted arena is no obstacle
static void test_get_or_insert_of_a_present_key_needs_no_allocator() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 512);
    TEST_ASSERT_NOT_NULL(arena);

    tda_HMap *m = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, int32_t, tda_hash_i32, tda_eq_i32, arena, &m));
    put(m, 1, 10);
    tda_test_arena_leave(arena, 0);

    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, m, 1, 999, &node));
    TEST_ASSERT_EQUAL_INT32(10, *TDA_HMAP_NODE_VAL_AS(int32_t, m, node));

    tda_al_arena_drop(arena);
}

/* ========== compare ========== */

// an equality over the value side that sees less than the bytes do, to show that what
// counts as an equal value is the caller's call and not the map's
static bool eq_i32_abs(const void *lhs, const void *rhs) {
    const int32_t a = *(const int32_t *) lhs;
    const int32_t b = *(const int32_t *) rhs;

    return (a < 0 ? -a : a) == (b < 0 ? -b : b);
}

// neither the order the entries went in nor the number of buckets they landed in is part
// of what a map holds
static void test_eq_ignores_insertion_order_and_bucket_count() {
    tda_HMap *a = make_filled(tda_hash_i32, 40);

    tda_HMap *b = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW_CAP(int32_t, int32_t, 256, tda_hash_i32, tda_eq_i32, tda_al_default(), &b));
    for (int32_t i = 39; i >= 0; --i) {
        put(b, i, i * 10);
    }

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(a) != tda_hmap_bucket_count(b));
    TEST_ASSERT_TRUE(tda_hmap_eq(a, a));
    TEST_ASSERT_TRUE(tda_hmap_eq(a, b));
    TEST_ASSERT_TRUE(tda_hmap_eq(b, a));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
}

// the keys of 'a' are looked up in 'b', so it is the hasher of 'b' that has to answer:
// a hash taken from 'a' would point at the wrong bucket of a differently hashed table
static void test_eq_looks_the_keys_up_with_the_hasher_of_the_other() {
    tda_HMap *a = make_filled(tda_hash_i32, 12);
    tda_HMap *b = make_filled(hash_all_alike, 12);

    TEST_ASSERT_TRUE(tda_hmap_eq(a, b));
    TEST_ASSERT_TRUE(tda_hmap_eq(b, a));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
}

static void test_eq_parts_a_differing_value() {
    tda_HMap *a = make_filled(tda_hash_i32, 8);
    tda_HMap *b = make_filled(tda_hash_i32, 8);
    put(b, 3, -30);

    TEST_ASSERT_EQUAL_size_t(tda_hmap_len(a), tda_hmap_len(b));
    TEST_ASSERT_FALSE(tda_hmap_eq(a, b));
    TEST_ASSERT_FALSE(tda_hmap_eq(b, a));

    // ... unless the equality the caller names forgives the difference
    TEST_ASSERT_TRUE(tda_hmap_eq_by(a, b, eq_i32_abs));
    TEST_ASSERT_TRUE(tda_hmap_eq_by(b, a, eq_i32_abs));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
}

// the same number of entries, one key in place of another
static void test_eq_parts_a_differing_key() {
    tda_HMap *a = make_filled(tda_hash_i32, 8);
    tda_HMap *b = make_filled(tda_hash_i32, 8);
    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, b, 3));
    put(b, 100, 30);

    TEST_ASSERT_EQUAL_size_t(tda_hmap_len(a), tda_hmap_len(b));
    TEST_ASSERT_FALSE(tda_hmap_eq(a, b));
    TEST_ASSERT_FALSE(tda_hmap_eq(b, a));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
}

// one is a proper subset of the other, so only the length says no
static void test_eq_parts_different_lengths() {
    tda_HMap *a = make_filled(tda_hash_i32, 8);
    tda_HMap *smaller = make_filled(tda_hash_i32, 7);

    TEST_ASSERT_FALSE(tda_hmap_eq(a, smaller));
    TEST_ASSERT_FALSE(tda_hmap_eq(smaller, a));

    tda_hmap_drop(a);
    tda_hmap_drop(smaller);
}

static void test_eq_of_two_empties() {
    tda_HMap *a = make_map(tda_hash_i32);
    tda_HMap *b = make_filled(tda_hash_i32, 8);
    tda_HMap *one = make_filled(tda_hash_i32, 1);

    tda_hmap_clear(b);

    TEST_ASSERT_TRUE(tda_hmap_eq(a, b));
    TEST_ASSERT_TRUE(tda_hmap_eq(b, a));
    TEST_ASSERT_FALSE(tda_hmap_eq(a, one));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
    tda_hmap_drop(one);
}

static void test_eq_walks_whole_chains() {
    tda_HMap *a = make_filled(hash_all_alike, 16);
    tda_HMap *b = make_filled(hash_all_alike, 16);
    put(b, 15, -150);

    TEST_ASSERT_TRUE(tda_hmap_eq(a, a));
    TEST_ASSERT_FALSE(tda_hmap_eq(a, b));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
}

// a value with a field that does not count is what the second door is for: these Pairs
// agree in the first field and differ in the second
static void test_eq_by_asks_the_equality_for_the_value_side() {
    tda_HMap *a = nullptr;
    tda_HMap *b = nullptr;
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, Pair, tda_hash_i32, tda_eq_i32, tda_al_default(), &a));
    TDA_TEST_OK(TDA_HMAP_NEW(int32_t, Pair, tda_hash_i32, tda_eq_i32, tda_al_default(), &b));

    for (int32_t i = 0; i < 8; ++i) {
        TDA_TEST_OK(tda_hmap_insert(a, &i, &(Pair){i, 10}, nullptr));
        TDA_TEST_OK(tda_hmap_insert(b, &i, &(Pair){i, 70}, nullptr));
    }

    TEST_ASSERT_FALSE(tda_hmap_eq(a, b));
    TEST_ASSERT_TRUE(tda_hmap_eq_by(a, b, tda_test_pair_eq_a));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
}

// the two doors are one walk: wherever the equality IS the bytes they answer alike
static void test_eq_and_eq_by_agree_on_plain_values() {
    tda_HMap *a = make_filled(tda_hash_i32, 16);
    tda_HMap *b = make_filled(tda_hash_i32, 16);
    tda_HMap *smaller = make_filled(tda_hash_i32, 15);

    TEST_ASSERT_EQUAL(tda_hmap_eq(a, b), tda_hmap_eq_by(a, b, tda_eq_i32));
    TEST_ASSERT_EQUAL(tda_hmap_eq(a, smaller), tda_hmap_eq_by(a, smaller, tda_eq_i32));

    put(b, 7, -70);
    TEST_ASSERT_EQUAL(tda_hmap_eq(a, b), tda_hmap_eq_by(a, b, tda_eq_i32));

    tda_hmap_drop(a);
    tda_hmap_drop(b);
    tda_hmap_drop(smaller);
}

static void test_eq_matches_a_copy() {
    tda_HMap *a = make_filled(tda_hash_i32, 24);

    tda_HMap *copy = nullptr;
    TDA_TEST_OK(tda_hmap_copy(a, &copy));

    TEST_ASSERT_TRUE(tda_hmap_eq(a, copy));

    tda_hmap_drop(a);
    tda_hmap_drop(copy);
}

/* ========== print ========== */

// a printer writes to a stream, so a case reads one back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, const tda_HMap *m) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_hmap_fprint(m, stream, tda_fprint_i32, tda_fprint_i32);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof buf - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_entry() {
    tda_HMap *m = make_map(tda_hash_i32);
    put(m, 1, 10);

    assert_prints("{1: 10}\n", m);

    tda_hmap_drop(m);
}

// with more than one entry the order is the buckets', which the header calls unspecified —
// so a case may say what is printed only where there is nothing to order
static void test_fprint_of_an_empty_map() {
    tda_HMap *m = make_map(tda_hash_i32);

    assert_prints("{}\n", m);

    tda_hmap_drop(m);
}

// the stdout twin takes no stream and cannot be captured portably, so a case can only
// say that it runs and reaches the same printer
static void test_print_writes_to_stdout() {
    tda_HMap *m = make_filled(tda_hash_i32, 3);

    tda_hmap_print(m, tda_fprint_i32, tda_fprint_i32);

    tda_hmap_drop(m);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_starts_empty);
    RUN_TEST(test_new_takes_no_buckets_until_the_first_insert);
    RUN_TEST(test_new_cap_reserves_buckets_without_entries);
    RUN_TEST(test_drop_null_is_noop);
    RUN_TEST(test_drop_hands_back_every_node);

    RUN_TEST(test_insert_then_get);
    RUN_TEST(test_get_of_a_missing_key_is_null);
    RUN_TEST(test_get_on_an_empty_map_is_null);
    RUN_TEST(test_insert_overwrites_an_existing_key);
    RUN_TEST(test_insert_reports_whether_the_key_was_new);
    RUN_TEST(test_get_mut_writes_through);
    RUN_TEST(test_find_gives_the_entry_as_a_node);
    RUN_TEST(test_node_val_mut_writes_through);
    RUN_TEST(test_a_map_whose_keys_all_collide_still_finds_them);
    RUN_TEST(test_colliding_keys_overwrite_only_their_own_entry);
    RUN_TEST(test_a_hash_collision_is_not_an_equality);
    RUN_TEST(test_wide_keys_and_values_travel_whole);
    RUN_TEST(test_the_chain_is_walked_by_hash_before_eq);

    RUN_TEST(test_the_buckets_double_as_the_map_fills);
    RUN_TEST(test_every_entry_survives_the_growths);
    RUN_TEST(test_a_borrowed_node_survives_every_growth);
    RUN_TEST(test_a_growth_allocates_only_the_bucket_array);
    RUN_TEST(test_reserve_grows_the_buckets_only);
    RUN_TEST(test_reserve_below_the_bucket_count_changes_nothing);

    RUN_TEST(test_get_or_insert_puts_a_missing_key_in);
    RUN_TEST(test_get_or_insert_leaves_a_present_key_alone);
    RUN_TEST(test_get_or_insert_hands_back_the_node_that_is_already_there);
    RUN_TEST(test_get_or_insert_hashes_the_key_once);
    RUN_TEST(test_get_or_insert_carries_the_counter_idiom);

    RUN_TEST(test_shrink_to_fit_gives_the_buckets_back);
    RUN_TEST(test_shrink_to_fit_stops_at_the_starting_size);
    RUN_TEST(test_shrink_to_fit_of_an_empty_map_owns_nothing);
    RUN_TEST(test_shrink_to_fit_keeps_borrowed_nodes);
    RUN_TEST(test_shrink_to_fit_allocates_only_the_bucket_array);
    RUN_TEST(test_shrink_to_fit_when_already_tight_is_a_noop);
    RUN_TEST(test_a_shrunk_map_grows_again);

    RUN_TEST(test_remove_takes_the_entry_out);
    RUN_TEST(test_remove_of_a_missing_key_says_so);
    RUN_TEST(test_remove_on_an_empty_map_says_so);
    RUN_TEST(test_remove_from_the_middle_of_a_chain);
    RUN_TEST(test_remove_every_key_of_a_chain_in_turn);
    RUN_TEST(test_mut_walk_writes_through_every_entry);
    RUN_TEST(test_mut_walk_of_an_empty_map_stops_at_once);
    RUN_TEST(test_remove_node_drops_the_entry_it_names);
    RUN_TEST(test_a_key_can_be_put_back_after_removal);
    RUN_TEST(test_clear_empties_and_keeps_the_buckets);
    RUN_TEST(test_clear_leaves_a_usable_map);

    RUN_TEST(test_the_walk_reaches_every_entry_once);
    RUN_TEST(test_the_walk_crosses_a_single_chain);
    RUN_TEST(test_the_walk_skips_the_empty_buckets);
    RUN_TEST(test_the_walk_of_an_empty_map_stops_at_once);
    RUN_TEST(test_the_walk_after_a_removal_sees_the_rest);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_carries_the_hasher_and_the_equality);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_empties_the_source);
    RUN_TEST(test_move_assign_across_allocators_reports_an_exhausted_arena);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_of_empty_stays_empty);
    RUN_TEST(test_copy_assign_overwrites_the_target);
    RUN_TEST(test_copy_assign_hands_over_the_hasher_too);
    RUN_TEST(test_copy_assign_self_is_noop);
    RUN_TEST(test_copy_assign_keeps_the_target_allocator);
    RUN_TEST(test_copy_assign_hands_back_the_old_contents);

    RUN_TEST(test_swap_exchanges_the_entries_and_the_hashers);
    RUN_TEST(test_swap_keeps_the_nodes_alive);
    RUN_TEST(test_swap_self_is_noop);

    RUN_TEST(test_new_reports_an_exhausted_arena);
    RUN_TEST(test_new_cap_frees_the_header_when_the_buckets_are_refused);
    RUN_TEST(test_insert_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_an_overwrite_needs_no_allocator);
    RUN_TEST(test_reserve_reports_an_exhausted_arena);
    RUN_TEST(test_copy_reports_an_exhausted_arena);
    RUN_TEST(test_copy_assign_leaves_the_target_untouched_on_failure);
    RUN_TEST(test_a_refused_node_leaves_nothing_behind);
    RUN_TEST(test_get_or_insert_reports_an_exhausted_arena);
    RUN_TEST(test_get_or_insert_of_a_present_key_needs_no_allocator);


    RUN_TEST(test_eq_ignores_insertion_order_and_bucket_count);
    RUN_TEST(test_eq_looks_the_keys_up_with_the_hasher_of_the_other);
    RUN_TEST(test_eq_parts_a_differing_value);
    RUN_TEST(test_eq_parts_a_differing_key);
    RUN_TEST(test_eq_parts_different_lengths);
    RUN_TEST(test_eq_of_two_empties);
    RUN_TEST(test_eq_walks_whole_chains);
    RUN_TEST(test_eq_by_asks_the_equality_for_the_value_side);
    RUN_TEST(test_eq_and_eq_by_agree_on_plain_values);
    RUN_TEST(test_eq_matches_a_copy);

    RUN_TEST(test_fprint_writes_the_entry);
    RUN_TEST(test_fprint_of_an_empty_map);
    RUN_TEST(test_print_writes_to_stdout);

    return UNITY_END();
}
