#include "tda/ds/hmap.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
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

// every key in one bucket; otherwise chains stay one node long and the code that walks
// them is never reached
static tda_Hash hash_all_alike(const void *, void *) {
    return 0;
}

// at most two keys per bucket at any size: chains, without collapsing the map into one
static tda_Hash hash_by_parity(const void *key, void *) {
    return (tda_Hash) (*(const int32_t *) key & 1);
}

// hasher and equality for Pair as core/hash prescribes: the hash is folded from the
// fields, not taken over the bytes, since padding is not part of the value
static tda_Hash hash_pair(const void *key, void *) {
    const Pair *p = key;

    return tda_hash_combine(tda_hash_i64(&p->a, nullptr), tda_hash_i64(&p->b, nullptr));
}

static bool eq_pair(const void *lhs, const void *rhs, void *) {
    const Pair *a = lhs, *b = rhs;

    return a->a == b->a && a->b == b->b;
}

// hashes that differ in the high bits and agree in the low ones: one bucket for every
// key, yet no two hashes equal
static tda_Hash hash_high_bits_only(const void *key, void *) {
    return ((tda_Hash) *(const int32_t *) key) << 32;
}

static size_t hash_calls = 0;

static tda_Hash hash_counting_i32(const void *key, void *) {
    ++hash_calls;

    return tda_hash_i32(key, nullptr);
}

static size_t eq_calls = 0;

static bool eq_counting_i32(const void *lhs, const void *rhs, void *) {
    ++eq_calls;

    return tda_eq_i32(lhs, rhs, nullptr);
}

[[nodiscard]]
static tda_HMap make_map(tda_Hasher hasher, void *ctx) {
    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, hasher, tda_eq_i32, ctx, tda_al_default());

    return m;
}

static void put(tda_HMap *m, int32_t key, int32_t val) {
    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, m, key, val, nullptr));
}

// 'key * 10' under every key from 0 to n - 1
[[nodiscard]]
static tda_HMap make_filled(tda_Hasher hasher, void *ctx, int32_t n) {
    tda_HMap m = make_map(hasher, ctx);
    for (int32_t i = 0; i < n; ++i) {
        put(&m, i, i * 10);
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

// the walk reaches exactly 'len' entries, each answering get with its own value; the
// order is unspecified, so that is all a walk can promise
static void assert_walk_sees_everything(const tda_HMap *m) {
    size_t seen = 0;
    TDA_HMAP_FOR_EACH(node, m) {
        const int32_t key = *TDA_HMAP_NODE_KEY_AS(int32_t, m, node);
        const int32_t val = *TDA_HMAP_NODE_VAL_AS(int32_t, m, node);
        assert_has(m, key, val);
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(tda_hmap_len(m), seen);
}

/* ========== lifetime ========== */

static void test_init_starts_empty() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&m));
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_bucket_count(&m));
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_hmap_key_layout(&m).size);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_hmap_val_layout(&m).size);
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_hmap_al(&m)));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hmap_hasher(&m));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hmap_key_eq(&m));
    TEST_ASSERT_NULL(tda_hmap_first_node(&m));

    tda_hmap_deinit(&m);
}

// an empty map owns no buckets; the array is taken on the first insert
static void test_init_takes_no_buckets_until_the_first_insert() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    put(&m, 1, 10);
    TEST_ASSERT_EQUAL_size_t(2, probe.live); // the buckets and one node

    tda_hmap_deinit(&m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_init_cap_reserves_buckets_without_entries() {
    tda_HMap m = {};
    TDA_TEST_OK(TDA_HMAP_INIT_CAP(int32_t, int32_t, 100, tda_hash_i32, tda_eq_i32, nullptr, tda_al_default(), &m));

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&m));
    TEST_ASSERT_TRUE(tda_hmap_bucket_count(&m) >= 100);

    tda_hmap_deinit(&m);
}

static void test_deinit_of_an_inert_value_is_noop() {
    tda_HMap x = {};
    tda_hmap_deinit(&x);
    tda_hmap_deinit(&x);
}

static void test_deinit_hands_back_everything() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    for (int32_t i = 0; i < 20; ++i) {
        put(&m, i, i);
    }

    tda_hmap_deinit(&m);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== insert and lookup ========== */

static void test_insert_then_get() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    put(&m, 1, 10);
    put(&m, 2, 20);

    assert_has(&m, 1, 10);
    assert_has(&m, 2, 20);
    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(&m));

    tda_hmap_deinit(&m);
}

static void test_get_of_a_missing_key_is_null() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 1, 10);

    assert_missing(&m, 2);
    assert_missing(&m, -1);

    tda_hmap_deinit(&m);
}

static void test_get_on_an_empty_map_is_null() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    assert_missing(&m, 1);
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&m));

    tda_hmap_deinit(&m);
}

static void test_insert_overwrites_an_existing_key() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    put(&m, 1, 10);
    put(&m, 1, 99);

    assert_has(&m, 1, 99);
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m)); // an overwrite is not a second entry

    tda_hmap_deinit(&m);
}

// the new value may be the one it overwrites, as returned by a lookup
static void test_insert_over_a_key_with_its_own_value() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 1, 10);

    TDA_TEST_OK(tda_hmap_insert(&m, &(int32_t){1}, tda_hmap_get(&m, &(int32_t){1}), nullptr));

    assert_has(&m, 1, 10);

    tda_hmap_deinit(&m);
}

static void test_insert_reports_whether_the_key_was_new() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    bool is_new = false;
    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &m, 1, 10, &is_new));
    TEST_ASSERT_TRUE(is_new);

    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &m, 1, 20, &is_new));
    TEST_ASSERT_FALSE(is_new);

    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &m, 2, 20, &is_new));
    TEST_ASSERT_TRUE(is_new);

    tda_hmap_deinit(&m);
}

static void test_get_mut_writes_through() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 1, 10);

    *TDA_HMAP_GET_MUT_AS(int32_t, int32_t, &m, 1) = 42;

    assert_has(&m, 1, 42);

    tda_hmap_deinit(&m);
}

static void test_find_gives_the_entry_as_a_node() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 7, 70);

    const tda_HMapNode *node = TDA_HMAP_FIND(int32_t, &m, 7);
    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT32(7, *TDA_HMAP_NODE_KEY_AS(int32_t, &m, node));
    TEST_ASSERT_EQUAL_INT32(70, *TDA_HMAP_NODE_VAL_AS(int32_t, &m, node));

    tda_hmap_deinit(&m);
}

static void test_node_val_mut_writes_through() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 7, 70);

    tda_HMapNode *node = TDA_HMAP_FIND_MUT(int32_t, &m, 7);
    *TDA_HMAP_NODE_VAL_MUT_AS(int32_t, &m, node) = 77;

    assert_has(&m, 7, 77);

    tda_hmap_deinit(&m);
}

// every key in one bucket, so lookup walks a chain instead of hitting its head
static void test_a_map_whose_keys_all_collide_still_finds_them() {
    tda_HMap m = make_filled(hash_all_alike, nullptr, 20);

    TEST_ASSERT_EQUAL_size_t(20, tda_hmap_len(&m));
    for (int32_t i = 0; i < 20; ++i) {
        assert_has(&m, i, i * 10);
    }
    assert_missing(&m, 20);

    tda_hmap_deinit(&m);
}

static void test_colliding_keys_overwrite_only_their_own_entry() {
    tda_HMap m = make_filled(hash_all_alike, nullptr, 5);

    put(&m, 3, 999);

    TEST_ASSERT_EQUAL_size_t(5, tda_hmap_len(&m));
    assert_has(&m, 0, 0);
    assert_has(&m, 3, 999);
    assert_has(&m, 4, 40);

    tda_hmap_deinit(&m);
}

// a hash that agrees where the keys do not: 'eq' decides
static void test_a_hash_collision_is_not_an_equality() {
    tda_HMap m = make_map(hash_all_alike, nullptr);

    put(&m, 1, 10);
    put(&m, 2, 20);

    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(&m));
    assert_has(&m, 1, 10);
    assert_has(&m, 2, 20);

    tda_hmap_deinit(&m);
}

static void test_wide_keys_and_values_travel_whole() {
    tda_HMap m = TDA_HMAP_INIT(Pair, Pair, hash_pair, eq_pair, nullptr, tda_al_default());

    constexpr Pair key = {1, 2};
    constexpr Pair val = {30, 40};
    TDA_TEST_OK(tda_hmap_insert(&m, &key, &val, nullptr));

    const Pair *got = tda_hmap_get(&m, &key);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_INT64(30, got->a);
    TEST_ASSERT_EQUAL_INT64(40, got->b);
    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_hmap_key_layout(&m).size);

    tda_hmap_deinit(&m);
}

// the cached hash at work: in a chain of ten only the node whose hash matches reaches
// 'eq', and a miss never calls it
static void test_the_chain_is_walked_by_hash_before_eq() {
    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, hash_high_bits_only, eq_counting_i32, nullptr, tda_al_default());

    for (int32_t i = 0; i < 10; ++i) {
        put(&m, i, i * 10);
    }
    eq_calls = 0;
    const int32_t *got = TDA_HMAP_GET_AS(int32_t, int32_t, &m, 7);
    TEST_ASSERT_NOT_NULL(got);
    TEST_ASSERT_EQUAL_INT32(70, *got);
    TEST_ASSERT_EQUAL_size_t(1, eq_calls);

    eq_calls = 0;
    assert_missing(&m, 100);
    TEST_ASSERT_EQUAL_size_t(0, eq_calls);

    tda_hmap_deinit(&m);
}

/* ========== growth ========== */

static void test_the_buckets_double_as_the_map_fills() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    put(&m, 0, 0);
    const size_t first = tda_hmap_bucket_count(&m);
    TEST_ASSERT_TRUE(first > 0);

    for (int32_t i = 1; i < 200; ++i) {
        put(&m, i, i);
    }

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(&m) > first);
    TEST_ASSERT_TRUE(tda_hmap_len(&m) <= tda_hmap_bucket_count(&m));

    tda_hmap_deinit(&m);
}

static void test_every_entry_survives_the_growths() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 300);

    TEST_ASSERT_EQUAL_size_t(300, tda_hmap_len(&m));
    for (int32_t i = 0; i < 300; ++i) {
        assert_has(&m, i, i * 10);
    }
    assert_missing(&m, 300);

    tda_hmap_deinit(&m);
}

// the reason for chaining: growth reallocates the bucket array and relinks, so a node
// borrowed before it is still that entry afterwards
static void test_a_borrowed_node_survives_every_growth() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    for (int32_t i = 0; i < 4; ++i) {
        put(&m, i, i * 10);
    }

    const tda_HMapNode *held[4];
    for (int32_t i = 0; i < 4; ++i) {
        held[i] = TDA_HMAP_FIND(int32_t, &m, i);
        TEST_ASSERT_NOT_NULL(held[i]);
    }

    for (int32_t i = 4; i < 500; ++i) {
        put(&m, i, i * 10);
    }

    for (int32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_PTR(held[i], TDA_HMAP_FIND(int32_t, &m, i));
        TEST_ASSERT_EQUAL_INT32(i, *TDA_HMAP_NODE_KEY_AS(int32_t, &m, held[i]));
        TEST_ASSERT_EQUAL_INT32(i * 10, *TDA_HMAP_NODE_VAL_AS(int32_t, &m, held[i]));
    }

    tda_hmap_deinit(&m);
}

// growing moves no entry, so a rehash allocates only the bucket array
static void test_a_growth_allocates_only_the_bucket_array() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap m = {};
    TDA_TEST_OK(TDA_HMAP_INIT_CAP(int32_t, int32_t, 8, tda_hash_i32, tda_eq_i32, nullptr, al, &m));
    for (int32_t i = 0; i < 8; ++i) {
        put(&m, i, i);
    }

    const size_t before = tda_test_probe_requests(&probe);
    put(&m, 8, 8); // the one that pushes the load past one per bucket

    // one array plus one node, nothing per entry moved
    TEST_ASSERT_EQUAL_size_t(2, tda_test_probe_requests(&probe) - before);

    tda_hmap_deinit(&m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_reserve_grows_the_buckets_only() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 4);

    TDA_TEST_OK(tda_hmap_reserve(&m, 1000));

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(&m) >= 1000);
    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(&m));
    for (int32_t i = 0; i < 4; ++i) {
        assert_has(&m, i, i * 10);
    }

    tda_hmap_deinit(&m);
}

static void test_reserve_below_the_bucket_count_changes_nothing() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 20);
    const size_t before = tda_hmap_bucket_count(&m);

    TDA_TEST_OK(tda_hmap_reserve(&m, 1));

    TEST_ASSERT_EQUAL_size_t(before, tda_hmap_bucket_count(&m));

    tda_hmap_deinit(&m);
}

// no bucket count holds SIZE_MAX entries, and the refusal comes before any allocation
static void test_reserve_reports_a_count_no_bucket_array_holds() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 3);
    const size_t buckets = tda_hmap_bucket_count(&m);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_reserve(&m, SIZE_MAX));

    TEST_ASSERT_EQUAL_size_t(buckets, tda_hmap_bucket_count(&m));
    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));

    tda_hmap_deinit(&m);
}

/* ========== remove ========== */

static void test_remove_takes_the_entry_out() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 5);

    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, 2));

    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(&m));
    assert_missing(&m, 2);
    assert_has(&m, 1, 10);
    assert_has(&m, 3, 30);

    tda_hmap_deinit(&m);
}

static void test_remove_of_a_missing_key_says_so() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 3);

    TEST_ASSERT_FALSE(TDA_HMAP_REMOVE(int32_t, &m, 99));
    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));

    tda_hmap_deinit(&m);
}

static void test_remove_on_an_empty_map_says_so() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    TEST_ASSERT_FALSE(TDA_HMAP_REMOVE(int32_t, &m, 1));

    tda_hmap_deinit(&m);
}

// unlinking from the middle of a chain, which the head-of-bucket path never reaches
static void test_remove_from_the_middle_of_a_chain() {
    tda_HMap m = make_filled(hash_all_alike, nullptr, 5);

    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, 2));

    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(&m));
    assert_missing(&m, 2);
    for (int32_t i = 0; i < 5; ++i) {
        if (i != 2) {
            assert_has(&m, i, i * 10);
        }
    }

    tda_hmap_deinit(&m);
}

static void test_remove_every_key_of_a_chain_in_turn() {
    tda_HMap m = make_filled(hash_all_alike, nullptr, 6);

    for (int32_t i = 0; i < 6; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, i));
        TEST_ASSERT_EQUAL_size_t((size_t) (5 - i), tda_hmap_len(&m));
    }

    TEST_ASSERT_NULL(tda_hmap_first_node(&m));

    tda_hmap_deinit(&m);
}

// the mutable walk writes through the value: every entry doubled in one pass, with no
// lookup per key
static void test_mut_walk_writes_through_every_entry() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 4);

    size_t seen = 0;
    for (tda_HMapNode *node = tda_hmap_first_node_mut(&m); node; node = tda_hmap_node_next_mut(&m, node)) {
        *TDA_HMAP_NODE_VAL_MUT_AS(int32_t, &m, node) *= 2;
        ++seen;
    }
    TEST_ASSERT_EQUAL_size_t(4, seen);

    for (int32_t key = 0; key < 4; ++key) {
        TEST_ASSERT_EQUAL_INT32(key * 20, *TDA_HMAP_GET_AS(int32_t, int32_t, &m, key));
    }

    tda_hmap_deinit(&m);
}

static void test_mut_walk_of_an_empty_map_stops_at_once() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    TEST_ASSERT_NULL(tda_hmap_first_node_mut(&m));

    tda_hmap_deinit(&m);
}

static void test_remove_node_drops_the_entry_it_names() {
    tda_HMap m = make_filled(hash_all_alike, nullptr, 4);

    tda_HMapNode *node = tda_hmap_find_mut(&m, &(int32_t){2});
    TEST_ASSERT_NOT_NULL(node);

    tda_hmap_remove_node(&m, node);

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));
    assert_missing(&m, 2);
    assert_has(&m, 3, 30);

    tda_hmap_deinit(&m);
}

static void test_take_hands_the_key_and_the_value_out() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 5);

    int32_t key = -1, val = -1;
    TEST_ASSERT_TRUE(TDA_HMAP_TAKE(int32_t, int32_t, &m, 3, &key, &val));

    TEST_ASSERT_EQUAL_INT32(3, key);
    TEST_ASSERT_EQUAL_INT32(30, val);
    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(&m));
    assert_missing(&m, 3);
    assert_walk_sees_everything(&m);

    tda_hmap_deinit(&m);
}

static void test_take_of_a_missing_key_writes_nothing() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 3);

    int32_t key = -1, val = -1;
    TEST_ASSERT_FALSE(TDA_HMAP_TAKE(int32_t, int32_t, &m, 99, &key, &val));

    TEST_ASSERT_EQUAL_INT32(-1, key);
    TEST_ASSERT_EQUAL_INT32(-1, val);
    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));

    tda_hmap_deinit(&m);
}

// either side may be left behind; with both left behind, take is remove
static void test_take_with_null_outs() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 4);

    int32_t val = -1;
    TEST_ASSERT_TRUE(TDA_HMAP_TAKE(int32_t, int32_t, &m, 1, nullptr, &val));
    TEST_ASSERT_EQUAL_INT32(10, val);

    int32_t key = -1;
    TEST_ASSERT_TRUE(TDA_HMAP_TAKE(int32_t, int32_t, &m, 2, &key, nullptr));
    TEST_ASSERT_EQUAL_INT32(2, key);

    TEST_ASSERT_TRUE(tda_hmap_take(&m, &(int32_t){3}, nullptr, nullptr));
    TEST_ASSERT_FALSE(tda_hmap_take(&m, &(int32_t){3}, nullptr, nullptr));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));
    assert_has(&m, 0, 0);

    tda_hmap_deinit(&m);
}

static void test_take_on_an_empty_map_says_so() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    TEST_ASSERT_FALSE(TDA_HMAP_TAKE(int32_t, int32_t, &m, 1, nullptr, nullptr));

    tda_hmap_deinit(&m);
}

// every key of one chain in turn, each value with its own key
static void test_take_every_key_of_a_chain_in_turn() {
    tda_HMap m = make_filled(hash_all_alike, nullptr, 6);

    for (int32_t i = 5; i >= 0; --i) {
        int32_t val = -1;
        TEST_ASSERT_TRUE(TDA_HMAP_TAKE(int32_t, int32_t, &m, i, nullptr, &val));
        TEST_ASSERT_EQUAL_INT32(i * 10, val);
    }

    TEST_ASSERT_NULL(tda_hmap_first_node(&m));

    tda_hmap_deinit(&m);
}

// keys equal modulo 100: equality ignores what tells the stored key from the probe, and
// the stored one is what a map owning its keys has to free
static tda_Hash hash_mod_100(const void *key, void *) {
    return tda_hash_i32(&(int32_t){ *(const int32_t *) key % 100 }, nullptr);
}

static bool eq_mod_100(const void *lhs, const void *rhs, void *) {
    return *(const int32_t *) lhs % 100 == *(const int32_t *) rhs % 100;
}

// the stored key comes out, not the probe, even into the probe's own storage, which is
// read before it is written
static void test_take_hands_out_the_stored_key_over_the_probe() {
    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, hash_mod_100, eq_mod_100, nullptr, tda_al_default());
    put(&m, 105, 1);

    int32_t key = 5;
    TEST_ASSERT_TRUE(tda_hmap_take(&m, &key, &key, nullptr));

    TEST_ASSERT_EQUAL_INT32(105, key);
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&m));

    tda_hmap_deinit(&m);
}

static void test_take_moves_whole_keys_and_values() {
    tda_HMap m = TDA_HMAP_INIT(Pair, Pair, hash_pair, eq_pair, nullptr, tda_al_default());
    TDA_TEST_OK(tda_hmap_insert(&m, &(Pair){1, 2}, &(Pair){3, 4}, nullptr));

    Pair key = {0, 0};
    Pair val = {0, 0};
    TEST_ASSERT_TRUE(tda_hmap_take(&m, &(Pair){1, 2}, &key, &val));

    TEST_ASSERT_EQUAL_INT64(1, key.a);
    TEST_ASSERT_EQUAL_INT64(2, key.b);
    TEST_ASSERT_EQUAL_INT64(3, val.a);
    TEST_ASSERT_EQUAL_INT64(4, val.b);

    tda_hmap_deinit(&m);
}

static void test_a_key_can_be_put_back_after_removal() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 3);

    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, 1));
    put(&m, 1, 111);

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));
    assert_has(&m, 1, 111);

    tda_hmap_deinit(&m);
}

static void test_clear_empties_and_keeps_the_buckets() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 20);
    const size_t buckets = tda_hmap_bucket_count(&m);

    tda_hmap_clear(&m);

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&m));
    TEST_ASSERT_EQUAL_size_t(buckets, tda_hmap_bucket_count(&m));
    TEST_ASSERT_NULL(tda_hmap_first_node(&m));
    assert_missing(&m, 1);

    tda_hmap_deinit(&m);
}

static void test_clear_leaves_a_usable_map() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 10);

    tda_hmap_clear(&m);
    put(&m, 5, 55);

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));
    assert_has(&m, 5, 55);

    tda_hmap_deinit(&m);
}

/* ========== walk ========== */

static void test_the_walk_reaches_every_entry_once() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 50);

    assert_walk_sees_everything(&m);

    tda_hmap_deinit(&m);
}

// one bucket holds everything, so the walk is one chain with nothing to skip
static void test_the_walk_crosses_a_single_chain() {
    tda_HMap m = make_filled(hash_all_alike, nullptr, 12);

    assert_walk_sees_everything(&m);

    tda_hmap_deinit(&m);
}

// two full buckets among many empty ones: the walk has to step over the gaps
static void test_the_walk_skips_the_empty_buckets() {
    tda_HMap m = {};
    TDA_TEST_OK(TDA_HMAP_INIT_CAP(int32_t, int32_t, 64, hash_by_parity, tda_eq_i32, nullptr, tda_al_default(), &m));
    for (int32_t i = 0; i < 10; ++i) {
        put(&m, i, i * 10);
    }

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(&m) >= 64);
    assert_walk_sees_everything(&m);

    tda_hmap_deinit(&m);
}

static void test_the_walk_of_an_empty_map_stops_at_once() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    TEST_ASSERT_NULL(tda_hmap_first_node(&m));

    tda_HMap cleared = make_filled(tda_hash_i32, nullptr, 5);
    tda_hmap_clear(&cleared);
    TEST_ASSERT_NULL(tda_hmap_first_node(&cleared));

    tda_hmap_deinit(&m);
    tda_hmap_deinit(&cleared);
}

static void test_the_walk_after_a_removal_sees_the_rest() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 30);

    for (int32_t i = 0; i < 30; i += 2) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, i));
    }

    TEST_ASSERT_EQUAL_size_t(15, tda_hmap_len(&m));
    assert_walk_sees_everything(&m);

    tda_hmap_deinit(&m);
}

static const tda_HMap *counted(const tda_HMap *m, size_t *calls) {
    ++*calls;

    return m;
}

// the map is read once: stepping needs it on every entry, from the macro's own copy
static void test_for_each_reads_its_map_once() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 20);

    size_t calls = 0;
    size_t seen = 0;
    TDA_HMAP_FOR_EACH(node, counted(&m, &calls)) {
        TDA_UNUSED(node);
        ++seen;
    }

    TEST_ASSERT_EQUAL_size_t(20, seen);
    TEST_ASSERT_EQUAL_size_t(1, calls);

    tda_hmap_deinit(&m);
}

/* ========== get_or_insert ========== */

static void test_get_or_insert_puts_a_missing_key_in() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, &m, 1, 10, &node));

    TEST_ASSERT_NOT_NULL(node);
    TEST_ASSERT_EQUAL_INT32(1, *TDA_HMAP_NODE_KEY_AS(int32_t, &m, node));
    TEST_ASSERT_EQUAL_INT32(10, *TDA_HMAP_NODE_VAL_AS(int32_t, &m, node));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));
    assert_has(&m, 1, 10);

    tda_hmap_deinit(&m);
}

// unlike insert, it does not overwrite: its value is only for an absent key
static void test_get_or_insert_leaves_a_present_key_alone() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 1, 10);

    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, &m, 1, 999, &node));

    TEST_ASSERT_EQUAL_INT32(10, *TDA_HMAP_NODE_VAL_AS(int32_t, &m, node));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));

    tda_hmap_deinit(&m);
}

static void test_get_or_insert_hands_back_the_node_that_is_already_there() {
    tda_HMap m = make_filled(hash_all_alike, nullptr, 5);
    const tda_HMapNode *held = TDA_HMAP_FIND(int32_t, &m, 3);

    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, &m, 3, 0, &node));

    TEST_ASSERT_EQUAL_PTR(held, node);

    tda_hmap_deinit(&m);
}

// the reason it exists: get_mut then insert hashes the key twice whenever it is absent
static void test_get_or_insert_hashes_the_key_once() {
    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, hash_counting_i32, tda_eq_i32, nullptr, tda_al_default());

    hash_calls = 0;
    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, &m, 1, 10, &node));
    TEST_ASSERT_EQUAL_size_t(1, hash_calls);

    // the get_mut + insert pair it replaces, on another absent key
    hash_calls = 0;
    if (!tda_hmap_get_mut(&m, &(int32_t){2})) {
        TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &m, 2, 20, nullptr));
    }
    TEST_ASSERT_EQUAL_size_t(2, hash_calls);

    // and on a present key, where both forms cost the same
    hash_calls = 0;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, &m, 1, 0, &node));
    TEST_ASSERT_EQUAL_size_t(1, hash_calls);

    tda_hmap_deinit(&m);
}

// the idiom it was added for: a tally that looks each key up once
static void test_get_or_insert_carries_the_counter_idiom() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    constexpr int32_t seen[6] = {3, 1, 3, 3, 1, 7};
    for (size_t i = 0; i < 6; ++i) {
        tda_HMapNode *node = nullptr;
        TDA_TEST_OK(tda_hmap_get_or_insert(&m, &seen[i], &(int32_t){ 0 }, &node));
        ++*TDA_HMAP_NODE_VAL_MUT_AS(int32_t, &m, node);
    }

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));
    assert_has(&m, 3, 3);
    assert_has(&m, 1, 2);
    assert_has(&m, 7, 1);

    tda_hmap_deinit(&m);
}

/* ========== shrink_to_fit ========== */

static void test_shrink_to_fit_gives_the_buckets_back() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 300);
    const size_t grown = tda_hmap_bucket_count(&m);

    for (int32_t i = 3; i < 300; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, i));
    }

    TDA_TEST_OK(tda_hmap_shrink_to_fit(&m));

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(&m) < grown);
    TEST_ASSERT_TRUE(tda_hmap_bucket_count(&m) >= tda_hmap_len(&m));
    for (int32_t i = 0; i < 3; ++i) {
        assert_has(&m, i, i * 10);
    }
    assert_walk_sees_everything(&m);

    tda_hmap_deinit(&m);
}

// the floor is a fresh map's bucket count: a nearly empty map lands exactly where a new
// one starts, not at some denser shape that still works
static void test_shrink_to_fit_stops_at_the_starting_size() {
    tda_HMap fresh = make_map(tda_hash_i32, nullptr);
    put(&fresh, 0, 0);
    const size_t base = tda_hmap_bucket_count(&fresh);

    tda_HMap m = make_filled(tda_hash_i32, nullptr, 300);
    for (int32_t i = 1; i < 300; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, i));
    }

    TDA_TEST_OK(tda_hmap_shrink_to_fit(&m));

    TEST_ASSERT_EQUAL_size_t(base, tda_hmap_bucket_count(&m));
    assert_has(&m, 0, 0);

    tda_hmap_deinit(&fresh);
    tda_hmap_deinit(&m);
}

// nothing left to hold, so the map owns nothing, as when it was made
static void test_shrink_to_fit_of_an_empty_map_owns_nothing() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    for (int32_t i = 0; i < 50; ++i) {
        put(&m, i, i);
    }
    tda_hmap_clear(&m);

    TDA_TEST_OK(tda_hmap_shrink_to_fit(&m));

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_bucket_count(&m));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
    TEST_ASSERT_NULL(tda_hmap_first_node(&m));

    // still a map: the next insert takes buckets again
    put(&m, 1, 10);
    assert_has(&m, 1, 10);

    tda_hmap_deinit(&m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// shrinking relinks the entries in place, as growing does, so a borrowed node survives
static void test_shrink_to_fit_keeps_borrowed_nodes() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 200);

    const tda_HMapNode *held[3];
    for (int32_t i = 0; i < 3; ++i) {
        held[i] = TDA_HMAP_FIND(int32_t, &m, i);
    }

    for (int32_t i = 3; i < 200; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, i));
    }
    TDA_TEST_OK(tda_hmap_shrink_to_fit(&m));

    for (int32_t i = 0; i < 3; ++i) {
        TEST_ASSERT_EQUAL_PTR(held[i], TDA_HMAP_FIND(int32_t, &m, i));
        TEST_ASSERT_EQUAL_INT32(i * 10, *TDA_HMAP_NODE_VAL_AS(int32_t, &m, held[i]));
    }

    tda_hmap_deinit(&m);
}

static void test_shrink_to_fit_allocates_only_the_bucket_array() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    for (int32_t i = 0; i < 100; ++i) {
        put(&m, i, i);
    }
    for (int32_t i = 2; i < 100; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, i));
    }

    const size_t before = tda_test_probe_requests(&probe);
    TDA_TEST_OK(tda_hmap_shrink_to_fit(&m));

    TEST_ASSERT_EQUAL_size_t(1, tda_test_probe_requests(&probe) - before);

    tda_hmap_deinit(&m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a map already at its smallest asks the allocator for nothing
static void test_shrink_to_fit_when_already_tight_is_a_noop() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    put(&m, 1, 10);

    TDA_TEST_OK(tda_hmap_shrink_to_fit(&m));
    const size_t buckets = tda_hmap_bucket_count(&m);
    const size_t before = tda_test_probe_requests(&probe);

    TDA_TEST_OK(tda_hmap_shrink_to_fit(&m));

    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));
    TEST_ASSERT_EQUAL_size_t(buckets, tda_hmap_bucket_count(&m));

    tda_hmap_deinit(&m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_a_shrunk_map_grows_again() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 100);
    for (int32_t i = 2; i < 100; ++i) {
        TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &m, i));
    }
    TDA_TEST_OK(tda_hmap_shrink_to_fit(&m));

    for (int32_t i = 2; i < 100; ++i) {
        put(&m, i, i * 10);
    }

    TEST_ASSERT_EQUAL_size_t(100, tda_hmap_len(&m));
    for (int32_t i = 0; i < 100; ++i) {
        assert_has(&m, i, i * 10);
    }

    tda_hmap_deinit(&m);
}

/* ========== copy ========== */

static void test_copy_is_independent() {
    tda_HMap src = make_filled(tda_hash_i32, nullptr, 10);

    tda_HMap dst = {};
    TDA_TEST_OK(tda_hmap_copy(&src, &dst));

    put(&dst, 0, 999);
    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &dst, 1));

    assert_has(&src, 0, 0);
    assert_has(&src, 1, 10);
    assert_has(&dst, 0, 999);
    assert_missing(&dst, 1);

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
}

static void test_copy_carries_the_hasher_and_the_equality() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4096, tda_al_default(), &arena));

    tda_HMap src = TDA_HMAP_INIT(int32_t, int32_t, hash_all_alike, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&src, 1, 10);

    tda_HMap dst = {};
    TDA_TEST_OK(tda_hmap_copy(&src, &dst));

    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(&dst));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hmap_key_eq(&dst));
    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_hmap_al(&dst)));
    assert_has(&dst, 1, 10);

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
    tda_arena_deinit(&arena);
}

static void test_copy_with_builds_on_the_given_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4096, tda_al_default(), &arena));

    tda_HMap src = make_filled(tda_hash_i32, nullptr, 8);

    tda_HMap dst = {};
    TDA_TEST_OK(tda_hmap_copy_with(&src, tda_arena_to_al(&arena), &dst));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_hmap_al(&dst)));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_hmap_al(&src)));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hmap_hasher(&dst));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hmap_key_eq(&dst));
    TEST_ASSERT_TRUE(tda_hmap_eq(&src, &dst));

    // the source is gone and the copy still answers: the nodes are its own
    tda_hmap_deinit(&src);
    assert_has(&dst, 3, 30);

    tda_hmap_deinit(&dst);
    tda_arena_deinit(&arena);
}

// the buckets and nodes come from the copy's allocator
static void test_copy_with_reports_an_exhausted_target_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4096, tda_al_default(), &arena));
    tda_test_arena_leave(&arena, 0);

    tda_HMap src = make_filled(tda_hash_i32, nullptr, 8);

    tda_HMap dst;
    TDA_TEST_POISON(dst);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_copy_with(&src, tda_arena_to_al(&arena), &dst));
    TDA_TEST_ASSERT_POISONED(dst);
    TEST_ASSERT_EQUAL_size_t(8, tda_hmap_len(&src));

    tda_hmap_deinit(&src);
    tda_arena_deinit(&arena);
}

// a copy that fails halfway gives back the buckets and every node it already took
static void test_copy_rolls_back_the_partial_copy() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap src = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    for (int32_t i = 0; i < 8; ++i) {
        put(&src, i, i * 10);
    }
    const size_t live = probe.live;

    tda_test_probe_fail_after_next(&probe, 3); // the buckets and two nodes, then refuse
    tda_HMap dst;
    TDA_TEST_POISON(dst);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_copy(&src, &dst));

    TDA_TEST_ASSERT_POISONED(dst);
    TEST_ASSERT_EQUAL_size_t(live, probe.live); // nothing of the copy survived

    probe.fail_after = SIZE_MAX;
    tda_hmap_deinit(&src);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_move_assign_hands_over_the_contents_on_one_allocator() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap src = TDA_HMAP_INIT(int32_t, int32_t, hash_all_alike, tda_eq_i32, nullptr, al);
    put(&src, 1, 10);
    put(&src, 2, 20);

    tda_HMap dst = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    put(&dst, 9, 90);

    const size_t requests = tda_test_probe_requests(&probe);
    tda_hmap_move_assign(&dst, &src);

    // nothing was allocated: the buckets and nodes changed hands
    TEST_ASSERT_EQUAL_size_t(requests, tda_test_probe_requests(&probe));

    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(&dst));
    assert_has(&dst, 1, 10);
    assert_has(&dst, 2, 20);

    // the hasher travels with the entries, or the table would be read under another hash
    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(&dst));

    // the source gives back everything, buckets included, like every container
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&src));
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_bucket_count(&src));

    // but keeps its own hasher: the swap that moved the table must not leave it the
    // target's old one, just as a move across two allocators does not
    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(&src));
    TEST_ASSERT_EQUAL_PTR(tda_eq_i32, tda_hmap_key_eq(&src));

    // and is still a map: the first insert makes new buckets
    put(&src, 5, 50);
    assert_has(&src, 5, 50);

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the allocator is part of the value: 'dst' takes the elems with the allocator they came
// from
static void test_move_assign_across_allocators_hands_the_allocator_over() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4096, tda_al_default(), &arena));

    tda_HMap src = make_filled(tda_hash_i32, nullptr, 8);

    tda_HMap dst = TDA_HMAP_INIT(int32_t, int32_t, hash_all_alike, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&dst, 9, 90);

    tda_hmap_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(8, tda_hmap_len(&dst));
    assert_has(&dst, 3, 30);
    assert_missing(&dst, 9);
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hmap_hasher(&dst));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_hmap_al(&dst)));

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&src));
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_bucket_count(&src));
    TEST_ASSERT_TRUE(tda_al_eq(tda_al_default(), tda_hmap_al(&src)));

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
    tda_arena_deinit(&arena);
}

// nothing is allocated, so an exhausted allocator on either side cannot stop it
static void test_move_assign_needs_no_memory() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4096, tda_al_default(), &arena));

    tda_HMap dst = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&dst, 9, 90);
    tda_test_arena_leave(&arena, 0);

    tda_HMap src = make_filled(tda_hash_i32, nullptr, 8);

    tda_hmap_move_assign(&dst, &src);

    TEST_ASSERT_EQUAL_size_t(8, tda_hmap_len(&dst));
    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&src));

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
    tda_arena_deinit(&arena);
}

static void test_move_assign_of_itself_changes_nothing() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 4);

    tda_hmap_move_assign(&m, &m);

    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(&m));
    assert_has(&m, 2, 20);

    tda_hmap_deinit(&m);
}

static void test_copy_of_empty_stays_empty() {
    tda_HMap src = make_map(tda_hash_i32, nullptr);

    tda_HMap dst = {};
    TDA_TEST_OK(tda_hmap_copy(&src, &dst));

    TEST_ASSERT_EQUAL_size_t(0, tda_hmap_len(&dst));

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
}

static void test_copy_assign_overwrites_the_target() {
    tda_HMap src = make_filled(tda_hash_i32, nullptr, 6);
    tda_HMap dst = make_filled(tda_hash_i32, nullptr, 2);
    put(&dst, 100, 100);

    TDA_TEST_OK(tda_hmap_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_size_t(6, tda_hmap_len(&dst));
    assert_missing(&dst, 100);
    for (int32_t i = 0; i < 6; ++i) {
        assert_has(&dst, i, i * 10);
    }

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
}

static void test_copy_assign_hands_over_the_hasher_too() {
    tda_HMap src = make_map(hash_all_alike, nullptr);
    put(&src, 1, 10);

    tda_HMap dst = make_map(tda_hash_i32, nullptr);
    put(&dst, 2, 20);

    TDA_TEST_OK(tda_hmap_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(&dst));
    assert_has(&dst, 1, 10);
    assert_missing(&dst, 2);

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
}

static void test_copy_assign_of_itself_changes_nothing() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 5);

    TDA_TEST_OK(tda_hmap_copy_assign(&m, &m));

    TEST_ASSERT_EQUAL_size_t(5, tda_hmap_len(&m));
    assert_has(&m, 3, 30);

    tda_hmap_deinit(&m);
}

static void test_copy_assign_keeps_the_target_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4096, tda_al_default(), &arena));

    tda_HMap src = make_filled(tda_hash_i32, nullptr, 4);

    tda_HMap dst = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));

    TDA_TEST_OK(tda_hmap_copy_assign(&dst, &src));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&arena), tda_hmap_al(&dst)));
    TEST_ASSERT_EQUAL_size_t(4, tda_hmap_len(&dst));

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);
    tda_arena_deinit(&arena);
}

// the target's old contents must go back to the allocator, not be stranded when the
// clone replaces them; the default allocator would not notice
static void test_copy_assign_hands_back_the_old_contents() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap dst = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    for (int32_t i = 0; i < 20; ++i) {
        put(&dst, i, i);
    }

    tda_HMap src = make_filled(tda_hash_i32, nullptr, 3);
    TDA_TEST_OK(tda_hmap_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&dst));

    tda_hmap_deinit(&src);
    tda_hmap_deinit(&dst);

    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== swap ========== */

static void test_swap_exchanges_the_entries_and_the_hashers() {
    tda_HMap a = make_map(tda_hash_i32, nullptr);
    put(&a, 1, 10);

    tda_HMap b = make_map(hash_all_alike, nullptr);
    put(&b, 2, 20);
    put(&b, 3, 30);

    tda_hmap_swap(&a, &b);

    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(&a));
    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&b));
    TEST_ASSERT_EQUAL_PTR(hash_all_alike, tda_hmap_hasher(&a));
    TEST_ASSERT_EQUAL_PTR(tda_hash_i32, tda_hmap_hasher(&b));
    assert_has(&a, 2, 20);
    assert_has(&b, 1, 10);

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
}

// the nodes change map without moving, which is why swap needs one allocator
static void test_swap_keeps_the_nodes_alive() {
    tda_HMap a = make_filled(tda_hash_i32, nullptr, 4), b = make_map(tda_hash_i32, nullptr);

    const tda_HMapNode *held = TDA_HMAP_FIND(int32_t, &a, 2);
    TEST_ASSERT_NOT_NULL(held);

    tda_hmap_swap(&a, &b);

    TEST_ASSERT_EQUAL_PTR(held, TDA_HMAP_FIND(int32_t, &b, 2));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
}

static void test_swap_of_itself_changes_nothing() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 3);

    tda_hmap_swap(&m, &m);

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));
    assert_has(&m, 1, 10);

    tda_hmap_deinit(&m);
}

/* ========== retain ========== */

// keeps an entry when the key's bit is set in the mask at 'ctx', checking that the
// value passed is the one stored under that key
static bool in_mask_with_its_val(const void *key, const void *val, void *ctx) {
    const int32_t k = *(const int32_t *) key;
    TEST_ASSERT_EQUAL_INT32(k * 10, *(const int32_t *) val);

    return *(const uint32_t *) ctx >> k & 1;
}

// every subset of six entries, on spread buckets and on one chain, where a removal
// relinks a neighbour instead of emptying a bucket
static void test_retain_keeps_exactly_the_passing_entries() {
    const tda_Hasher hashers[2] = {tda_hash_i32, hash_all_alike};

    for (size_t h = 0; h < 2; ++h) {
        for (uint32_t mask = 0; mask < 64; ++mask) {
            tda_HMap m = make_filled(hashers[h], nullptr, 6);

            tda_hmap_retain(&m, in_mask_with_its_val, &mask);

            size_t n = 0;
            for (int32_t i = 0; i < 6; ++i) {
                if (mask >> i & 1) {
                    assert_has(&m, i, i * 10);
                    ++n;
                } else {
                    assert_missing(&m, i);
                }
            }
            TEST_ASSERT_EQUAL_size_t(n, tda_hmap_len(&m));
            assert_walk_sees_everything(&m);

            tda_hmap_deinit(&m);
        }
    }
}

static bool count_and_keep_odd(const void *key, const void *, void *ctx) {
    size_t *seen = ctx;
    const int32_t k = *(const int32_t *) key;
    ++seen[k];

    return k % 2 != 0;
}

static void test_retain_asks_once_per_entry_and_keeps_the_nodes() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, hash_all_alike, tda_eq_i32, nullptr, al);
    for (int32_t i = 0; i < 6; ++i) {
        put(&m, i, i * 10);
    }
    const tda_HMapNode *kept = TDA_HMAP_FIND(int32_t, &m, 3);

    size_t seen[6] = {0};
    tda_hmap_retain(&m, count_and_keep_odd, seen);

    for (size_t i = 0; i < 6; ++i) {
        TEST_ASSERT_EQUAL_size_t(1, seen[i]);
    }
    TEST_ASSERT_EQUAL_PTR(kept, TDA_HMAP_FIND(int32_t, &m, 3));
    TEST_ASSERT_EQUAL_size_t(4, probe.live); // the buckets and three nodes

    tda_hmap_deinit(&m);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== from_spans / extend ========== */

static void test_from_spans_pairs_keys_with_vals_by_index() {
    tda_HMap m = {};
    TDA_TEST_OK(
        tda_hmap_from_spans(
            TDA_SPAN_OF(int32_t, 1, 2, 3),
            TDA_SPAN_OF(int32_t, 10, 20, 30),
            tda_hash_i32,
            tda_eq_i32,
            nullptr,
            tda_al_default(),
            &m
        )
    );

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));
    assert_has(&m, 1, 10);
    assert_has(&m, 2, 20);
    assert_has(&m, 3, 30);

    tda_hmap_deinit(&m);
}

// as with insert, a key given twice keeps its last value
static void test_from_spans_keeps_the_last_value_of_a_repeated_key() {
    tda_HMap m = {};
    TDA_TEST_OK(
        tda_hmap_from_spans(
            TDA_SPAN_OF(int32_t, 1, 2, 1),
            TDA_SPAN_OF(int32_t, 10, 20, 11),
            tda_hash_i32,
            tda_eq_i32,
            nullptr,
            tda_al_default(),
            &m
        )
    );

    TEST_ASSERT_EQUAL_size_t(2, tda_hmap_len(&m));
    assert_has(&m, 1, 11);
    assert_has(&m, 2, 20);

    tda_hmap_deinit(&m);
}

static void test_from_spans_takes_both_layouts_from_the_spans() {
    constexpr Pair vals[1] = {{7, 8}};

    tda_HMap m = {};
    TDA_TEST_OK(
        tda_hmap_from_spans(
            TDA_SPAN_OF(int32_t, 1),
            TDA_SPAN_FROM_DATA(Pair, vals, 1),
            tda_hash_i32,
            tda_eq_i32,
            nullptr,
            tda_al_default(),
            &m
        )
    );

    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tda_hmap_key_layout(&m).size);
    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), tda_hmap_val_layout(&m).size);
    TEST_ASSERT_EQUAL_INT64(8, TDA_HMAP_GET_AS(int32_t, Pair, &m, 1)->b);

    tda_hmap_deinit(&m);
}

static void test_extend_overwrites_present_keys_and_adds_new_ones() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 1, 10);
    put(&m, 5, 50);

    TDA_TEST_OK(tda_hmap_extend(&m, TDA_SPAN_OF(int32_t, 1, 2), TDA_SPAN_OF(int32_t, 100, 200)));

    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&m));
    assert_has(&m, 1, 100);
    assert_has(&m, 2, 200);
    assert_has(&m, 5, 50);

    tda_hmap_deinit(&m);
}

static void test_from_spans_fails_at_every_step_without_a_leak() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);
    const tda_Span keys = TDA_SPAN_OF(int32_t, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    const tda_Span vals = TDA_SPAN_OF(int32_t, 9, 8, 7, 6, 5, 4, 3, 2, 1);

    for (size_t n = 0;; ++n) {
        tda_test_probe_fail_after_next(&probe, n);

        tda_HMap m = {};
        const tda_Status st = tda_hmap_from_spans(
            keys,
            vals,
            tda_hash_i32,
            tda_eq_i32,
            nullptr,
            al,
            &m
        );
        if (TDA_STATUS_IS_OK(st)) {
            TEST_ASSERT_EQUAL_size_t(9, tda_hmap_len(&m));
            tda_hmap_deinit(&m);
            TEST_ASSERT_EQUAL_size_t(0, probe.live);
            break;
        }

        TEST_ASSERT_EQUAL(TDA_STATUS_ERR_NO_MEM, st);
        TEST_ASSERT_NULL(m.al_.vt);
        TEST_ASSERT_EQUAL_size_t(0, probe.live);
    }
}

/* ========== allocation failure ========== */

static void test_init_cap_leaves_nothing_behind_when_the_buckets_are_refused() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_test_probe_fail_after_next(&probe, 0);

    tda_HMap m = {};
    TDA_TEST_STATUS(
        TDA_STATUS_ERR_NO_MEM,
        TDA_HMAP_INIT_CAP(int32_t, int32_t, 16, tda_hash_i32, tda_eq_i32, nullptr, al, &m)
    );

    TEST_ASSERT_NULL(m.al_.vt);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_insert_reports_an_exhausted_arena_and_changes_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&m, 1, 10);
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HMAP_INSERT(int32_t, int32_t, &m, 2, 20, nullptr));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));
    assert_has(&m, 1, 10);
    assert_missing(&m, 2);

    tda_arena_deinit(&arena);
}

// overwriting an existing key needs no memory, so an exhausted arena does not stop it
static void test_an_overwrite_needs_no_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&m, 1, 10);
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &m, 1, 99, nullptr));
    assert_has(&m, 1, 99);

    tda_arena_deinit(&arena);
}

static void test_reserve_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&m, 1, 10);
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_reserve(&m, 100000));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));
    assert_has(&m, 1, 10);

    tda_arena_deinit(&arena);
}

static void test_copy_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_HMap src = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&src, 1, 10);
    tda_test_arena_leave(&arena, 0);

    tda_HMap dst;
    TDA_TEST_POISON(dst);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_copy(&src, &dst));
    TDA_TEST_ASSERT_POISONED(dst);

    tda_arena_deinit(&arena);
}

// a refused clone must leave the target whole, not half overwritten
static void test_copy_assign_reports_a_refused_block_and_changes_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_HMap src = make_filled(tda_hash_i32, nullptr, 50);

    tda_HMap dst = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&dst, 7, 70);
    tda_test_arena_leave(&arena, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_hmap_copy_assign(&dst, &src));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&dst));
    assert_has(&dst, 7, 70);

    tda_hmap_deinit(&src);
    tda_arena_deinit(&arena);
}

// a refused node leaves the map as it was and leaks nothing
static void test_a_refused_node_leaves_nothing_behind() {
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al al = tda_test_probe_full(&probe);

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al);
    put(&m, 1, 10);

    tda_test_probe_fail_after_next(&probe, 0);

    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HMAP_INSERT(int32_t, int32_t, &m, 2, 20, nullptr));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));
    assert_has(&m, 1, 10);

    tda_test_probe_reset(&probe);
    tda_hmap_deinit(&m);
}

static void test_get_or_insert_reports_an_exhausted_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&m, 1, 10);
    tda_test_arena_leave(&arena, 0);

    tda_HMapNode *node = nullptr;
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, &m, 2, 20, &node));

    TEST_ASSERT_EQUAL_size_t(1, tda_hmap_len(&m));
    assert_missing(&m, 2);

    tda_arena_deinit(&arena);
}

// finding a present key needs no memory, so an exhausted arena does not stop it
static void test_get_or_insert_of_a_present_key_needs_no_allocator() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(512, tda_al_default(), &arena));

    tda_HMap m = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_arena_to_al(&arena));
    put(&m, 1, 10);
    tda_test_arena_leave(&arena, 0);

    tda_HMapNode *node = nullptr;
    TDA_TEST_OK(TDA_HMAP_GET_OR_INSERT(int32_t, int32_t, &m, 1, 999, &node));
    TEST_ASSERT_EQUAL_INT32(10, *TDA_HMAP_NODE_VAL_AS(int32_t, &m, node));

    tda_arena_deinit(&arena);
}

/* ========== compare ========== */

// neither insertion order nor bucket count is part of what a map holds
static void test_eq_ignores_insertion_order_and_bucket_count() {
    tda_HMap a = make_filled(tda_hash_i32, nullptr, 40);

    tda_HMap b = {};
    TDA_TEST_OK(TDA_HMAP_INIT_CAP(int32_t, int32_t, 256, tda_hash_i32, tda_eq_i32, nullptr, tda_al_default(), &b));
    for (int32_t i = 39; i >= 0; --i) {
        put(&b, i, i * 10);
    }

    TEST_ASSERT_TRUE(tda_hmap_bucket_count(&a) != tda_hmap_bucket_count(&b));
    TEST_ASSERT_TRUE(tda_hmap_eq(&a, &a));
    TEST_ASSERT_TRUE(tda_hmap_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_hmap_eq(&b, &a));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
}

// the keys of 'a' are looked up in 'b', so the hasher of 'b' must answer: a hash from
// 'a' would pick the wrong bucket in a differently hashed table
static void test_eq_looks_the_keys_up_with_the_hasher_of_the_other() {
    tda_HMap a = make_filled(tda_hash_i32, nullptr, 12);
    tda_HMap b = make_filled(hash_all_alike, nullptr, 12);

    TEST_ASSERT_TRUE(tda_hmap_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_hmap_eq(&b, &a));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
}

static void test_eq_parts_a_differing_value() {
    tda_HMap a = make_filled(tda_hash_i32, nullptr, 8), b = make_filled(tda_hash_i32, nullptr, 8);
    put(&b, 3, -30);

    TEST_ASSERT_EQUAL_size_t(tda_hmap_len(&a), tda_hmap_len(&b));
    TEST_ASSERT_FALSE(tda_hmap_eq(&a, &b));
    TEST_ASSERT_FALSE(tda_hmap_eq(&b, &a));

    // ... unless the caller's equality forgives the difference
    TEST_ASSERT_TRUE(tda_hmap_eq_by(&a, &b, tda_test_i32_eq_abs, nullptr));
    TEST_ASSERT_TRUE(tda_hmap_eq_by(&b, &a, tda_test_i32_eq_abs, nullptr));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
}

// the same number of entries, one key in place of another
static void test_eq_parts_a_differing_key() {
    tda_HMap a = make_filled(tda_hash_i32, nullptr, 8), b = make_filled(tda_hash_i32, nullptr, 8);
    TEST_ASSERT_TRUE(TDA_HMAP_REMOVE(int32_t, &b, 3));
    put(&b, 100, 30);

    TEST_ASSERT_EQUAL_size_t(tda_hmap_len(&a), tda_hmap_len(&b));
    TEST_ASSERT_FALSE(tda_hmap_eq(&a, &b));
    TEST_ASSERT_FALSE(tda_hmap_eq(&b, &a));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
}

// one is a proper subset of the other, so only the length says no
static void test_eq_parts_different_lengths() {
    tda_HMap a = make_filled(tda_hash_i32, nullptr, 8);
    tda_HMap smaller = make_filled(tda_hash_i32, nullptr, 7);

    TEST_ASSERT_FALSE(tda_hmap_eq(&a, &smaller));
    TEST_ASSERT_FALSE(tda_hmap_eq(&smaller, &a));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&smaller);
}

static void test_eq_of_two_empties() {
    tda_HMap a = make_map(tda_hash_i32, nullptr);
    tda_HMap b = make_filled(tda_hash_i32, nullptr, 8);
    tda_HMap one = make_filled(tda_hash_i32, nullptr, 1);

    tda_hmap_clear(&b);

    TEST_ASSERT_TRUE(tda_hmap_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_hmap_eq(&b, &a));
    TEST_ASSERT_FALSE(tda_hmap_eq(&a, &one));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
    tda_hmap_deinit(&one);
}

static void test_eq_walks_whole_chains() {
    tda_HMap a = make_filled(hash_all_alike, nullptr, 16);
    tda_HMap b = make_filled(hash_all_alike, nullptr, 16);
    put(&b, 15, -150);

    TEST_ASSERT_TRUE(tda_hmap_eq(&a, &a));
    TEST_ASSERT_FALSE(tda_hmap_eq(&a, &b));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
}

// a value field that does not count is what eq_by is for: these Pairs agree in the
// first field and differ in the second
static void test_eq_by_asks_the_equality_for_the_value_side() {
    tda_HMap a = TDA_HMAP_INIT(int32_t, Pair, tda_hash_i32, tda_eq_i32, nullptr, tda_al_default());
    tda_HMap b = TDA_HMAP_INIT(int32_t, Pair, tda_hash_i32, tda_eq_i32, nullptr, tda_al_default());

    for (int32_t i = 0; i < 8; ++i) {
        TDA_TEST_OK(tda_hmap_insert(&a, &i, &(Pair){i, 10}, nullptr));
        TDA_TEST_OK(tda_hmap_insert(&b, &i, &(Pair){i, 70}, nullptr));
    }

    TEST_ASSERT_FALSE(tda_hmap_eq(&a, &b));
    TEST_ASSERT_TRUE(tda_hmap_eq_by(&a, &b, tda_test_pair_eq_a, nullptr));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
}

// eq and eq_by share one walk: where the equality is the bytes, they agree
static void test_eq_and_eq_by_agree_on_plain_values() {
    tda_HMap a = make_filled(tda_hash_i32, nullptr, 16);
    tda_HMap b = make_filled(tda_hash_i32, nullptr, 16);
    tda_HMap smaller = make_filled(tda_hash_i32, nullptr, 15);

    TEST_ASSERT_EQUAL(tda_hmap_eq(&a, &b), tda_hmap_eq_by(&a, &b, tda_eq_i32, nullptr));
    TEST_ASSERT_EQUAL(tda_hmap_eq(&a, &smaller), tda_hmap_eq_by(&a, &smaller, tda_eq_i32, nullptr));

    put(&b, 7, -70);
    TEST_ASSERT_EQUAL(tda_hmap_eq(&a, &b), tda_hmap_eq_by(&a, &b, tda_eq_i32, nullptr));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&b);
    tda_hmap_deinit(&smaller);
}

static void test_eq_matches_a_copy() {
    tda_HMap a = make_filled(tda_hash_i32, nullptr, 24);

    tda_HMap copy = {};
    TDA_TEST_OK(tda_hmap_copy(&a, &copy));

    TEST_ASSERT_TRUE(tda_hmap_eq(&a, &copy));

    tda_hmap_deinit(&a);
    tda_hmap_deinit(&copy);
}

/* ========== print ========== */

// printers write to a stream; cases read it back via tmpfile, like test/core/test_print.c
static void assert_prints(const char *expected, const tda_HMap *m) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_hmap_fprint(m, stream, tda_fprint_i32, nullptr, tda_fprint_i32, nullptr);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

static void test_fprint_writes_the_entry() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 1, 10);

    assert_prints("{1: 10}", &m);

    tda_hmap_deinit(&m);
}

// a printer that writes its ctx, a tag, before the value
static int fprint_tagged(FILE *stream, const void *val, void *ctx) {
    return fprintf(stream, "%s%d", (const char *) ctx, *(const int32_t *) val);
}

// each printer gets its own ctx: the key's is not the value's
static void test_fprint_hands_each_printer_its_own_ctx() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);
    put(&m, 1, 10);

    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);
    TEST_ASSERT_EQUAL_INT(0, tda_hmap_fprint(&m, stream, fprint_tagged, "k", fprint_tagged, "v"));
    rewind(stream);

    char buf[32];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING("{k1: v10}", buf);

    tda_hmap_deinit(&m);
}

// with more than one entry the order is the buckets', which is unspecified, so a case
// states the output only where there is nothing to order
static void test_fprint_of_an_empty_map() {
    tda_HMap m = make_map(tda_hash_i32, nullptr);

    assert_prints("{}", &m);

    tda_hmap_deinit(&m);
}

// the stdout twin cannot be captured portably, so a case only checks that it runs and
// reaches the same printer
static void test_print_writes_to_stdout() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 3);

    tda_hmap_print(&m, tda_fprint_i32, nullptr, tda_fprint_i32, nullptr);

    tda_hmap_deinit(&m);
}

// each side keeps the allocator its elems came from, and each is given back to it
static void test_swap_across_allocators_carries_each_allocator() {
    tda_TestProbe pa, pb;
    tda_test_probe_reset(&pa);
    tda_test_probe_reset(&pb);
    tda_Al al_a = tda_test_probe_full(&pa), al_b = tda_test_probe_full(&pb);

    tda_HMap a = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al_a);
    for (int32_t i = 0; i < 2; ++i) {
        TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &a, i, i, nullptr));
    }
    tda_HMap b = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, al_b);
    for (int32_t i = 0; i < 3; ++i) {
        TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &b, i, i, nullptr));
    }

    tda_hmap_swap(&a, &b);

    TEST_ASSERT_TRUE(tda_al_eq(al_b, tda_hmap_al(&a)));
    TEST_ASSERT_TRUE(tda_al_eq(al_a, tda_hmap_al(&b)));
    TEST_ASSERT_EQUAL_size_t(3, tda_hmap_len(&a));

    tda_hmap_deinit(&a);
    TEST_ASSERT_EQUAL_size_t(0, pb.live);
    tda_hmap_deinit(&b);
    TEST_ASSERT_EQUAL_size_t(0, pa.live);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_HMap x = TDA_HMAP_INIT(int32_t, int32_t, tda_hash_i32, tda_eq_i32, nullptr, tda_al_default());
    TDA_TEST_OK(TDA_HMAP_INSERT(int32_t, int32_t, &x, 1, 1, nullptr));

    tda_hmap_deinit(&x);
    TEST_ASSERT_NULL(x.al_.vt);

    tda_hmap_deinit(&x);
}


/* ========== print errors ========== */

// each printer gets its own ctx on every call, every entry is still written after a
// failure, and the first failure, here the key's, is what comes back
static void test_fprint_hands_on_both_ctxs_and_the_first_failure() {
    tda_HMap m = make_filled(tda_hash_i32, nullptr, 3);
    tda_TestPrint keys = {.fail_from = 2}, vals = {.fail_from = 3};
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    TEST_ASSERT_EQUAL_INT(
        -2,
        tda_hmap_fprint(&m, stream, tda_test_fprint_i32_failing, &keys, tda_test_fprint_i32_failing, &vals)
    );
    TEST_ASSERT_EQUAL_size_t(3, keys.calls);
    TEST_ASSERT_EQUAL_size_t(3, vals.calls);
    fclose(stream);

    keys = (tda_TestPrint){}, vals = (tda_TestPrint){.fail_from = 1};
    TEST_ASSERT_EQUAL_INT(
        -1,
        tda_hmap_print(&m, tda_test_fprint_i32_failing, &keys, tda_test_fprint_i32_failing, &vals)
    );

    FILE *ro = tda_test_unwritable_stream();
    TEST_ASSERT_LESS_THAN_INT(0, tda_hmap_fprint(&m, ro, tda_fprint_i32, nullptr, tda_fprint_i32, nullptr));
    fclose(ro);

    tda_hmap_deinit(&m);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_empty);
    RUN_TEST(test_init_takes_no_buckets_until_the_first_insert);
    RUN_TEST(test_init_cap_reserves_buckets_without_entries);
    RUN_TEST(test_deinit_of_an_inert_value_is_noop);
    RUN_TEST(test_deinit_twice_is_noop);
    RUN_TEST(test_deinit_hands_back_everything);

    RUN_TEST(test_insert_then_get);
    RUN_TEST(test_get_of_a_missing_key_is_null);
    RUN_TEST(test_get_on_an_empty_map_is_null);
    RUN_TEST(test_insert_overwrites_an_existing_key);
    RUN_TEST(test_insert_over_a_key_with_its_own_value);
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
    RUN_TEST(test_reserve_reports_a_count_no_bucket_array_holds);

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
    RUN_TEST(test_take_hands_the_key_and_the_value_out);
    RUN_TEST(test_take_of_a_missing_key_writes_nothing);
    RUN_TEST(test_take_with_null_outs);
    RUN_TEST(test_take_on_an_empty_map_says_so);
    RUN_TEST(test_take_every_key_of_a_chain_in_turn);
    RUN_TEST(test_take_hands_out_the_stored_key_over_the_probe);
    RUN_TEST(test_take_moves_whole_keys_and_values);
    RUN_TEST(test_a_key_can_be_put_back_after_removal);
    RUN_TEST(test_clear_empties_and_keeps_the_buckets);
    RUN_TEST(test_clear_leaves_a_usable_map);

    RUN_TEST(test_the_walk_reaches_every_entry_once);
    RUN_TEST(test_the_walk_crosses_a_single_chain);
    RUN_TEST(test_the_walk_skips_the_empty_buckets);
    RUN_TEST(test_the_walk_of_an_empty_map_stops_at_once);
    RUN_TEST(test_the_walk_after_a_removal_sees_the_rest);
    RUN_TEST(test_for_each_reads_its_map_once);

    RUN_TEST(test_copy_is_independent);
    RUN_TEST(test_copy_carries_the_hasher_and_the_equality);
    RUN_TEST(test_copy_with_builds_on_the_given_allocator);
    RUN_TEST(test_copy_with_reports_an_exhausted_target_arena);
    RUN_TEST(test_copy_rolls_back_the_partial_copy);
    RUN_TEST(test_move_assign_hands_over_the_contents_on_one_allocator);
    RUN_TEST(test_move_assign_across_allocators_hands_the_allocator_over);
    RUN_TEST(test_move_assign_needs_no_memory);
    RUN_TEST(test_move_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_of_empty_stays_empty);
    RUN_TEST(test_copy_assign_overwrites_the_target);
    RUN_TEST(test_copy_assign_hands_over_the_hasher_too);
    RUN_TEST(test_copy_assign_of_itself_changes_nothing);
    RUN_TEST(test_copy_assign_keeps_the_target_allocator);
    RUN_TEST(test_copy_assign_hands_back_the_old_contents);

    RUN_TEST(test_swap_exchanges_the_entries_and_the_hashers);
    RUN_TEST(test_swap_keeps_the_nodes_alive);
    RUN_TEST(test_swap_of_itself_changes_nothing);

    RUN_TEST(test_retain_keeps_exactly_the_passing_entries);
    RUN_TEST(test_retain_asks_once_per_entry_and_keeps_the_nodes);

    RUN_TEST(test_from_spans_pairs_keys_with_vals_by_index);
    RUN_TEST(test_from_spans_keeps_the_last_value_of_a_repeated_key);
    RUN_TEST(test_from_spans_takes_both_layouts_from_the_spans);
    RUN_TEST(test_extend_overwrites_present_keys_and_adds_new_ones);
    RUN_TEST(test_from_spans_fails_at_every_step_without_a_leak);
    RUN_TEST(test_init_cap_leaves_nothing_behind_when_the_buckets_are_refused);
    RUN_TEST(test_insert_reports_an_exhausted_arena_and_changes_nothing);
    RUN_TEST(test_an_overwrite_needs_no_allocator);
    RUN_TEST(test_reserve_reports_an_exhausted_arena);
    RUN_TEST(test_copy_reports_an_exhausted_arena);
    RUN_TEST(test_copy_assign_reports_a_refused_block_and_changes_nothing);
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
    RUN_TEST(test_fprint_hands_each_printer_its_own_ctx);
    RUN_TEST(test_fprint_of_an_empty_map);
    RUN_TEST(test_print_writes_to_stdout);

    RUN_TEST(test_swap_across_allocators_carries_each_allocator);

    RUN_TEST(test_fprint_hands_on_both_ctxs_and_the_first_failure);

    return UNITY_END();
}
