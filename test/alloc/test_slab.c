#include "tda/alloc/debug.h"
#include "tda/alloc/default.h"
#include "tda/alloc/slab.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/core/rng.h"
#include "tda/ds/bitset.h"
#include "tda/ds/deque.h"
#include "tda/ds/hmap.h"
#include "tda/ds/hset.h"
#include "tda/ds/list.h"
#include "tda/ds/pqueue.h"
#include "tda/ds/vec.h"

#include "support/probe.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========== probe allocator ==========
 *
 * The parent is the shared probe, so the tests see what reaches it: whole chunks for
 * the small blocks, the large blocks as they are.
 */

static constexpr size_t CHUNK = 4096;    // mirrors CHUNK_BYTES in slab.c
static constexpr size_t SMALL_MAX = 512; // mirrors SMALL_MAX

static tda_TestProbe probe;
static tda_Al parent;

void setUp() {
    tda_test_probe_reset(&probe);
    parent = tda_test_probe_full(&probe);
}

void tearDown() {
}

/* ========== lifetime ========== */

static void test_new_takes_two_blocks_and_drop_returns_them() {
    tda_Al *al = tda_al_slab_new(&parent);
    TEST_ASSERT_NOT_NULL(al);
    TEST_ASSERT_EQUAL_size_t(2, probe.live);

    tda_al_slab_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_new_cleans_up_after_a_failing_parent() {
    tda_test_probe_fail_after_next(&probe, 0);
    TEST_ASSERT_NULL(tda_al_slab_new(&parent));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    tda_test_probe_reset(&probe);
    tda_test_probe_fail_after_next(&probe, 1);
    TEST_ASSERT_NULL(tda_al_slab_new(&parent));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_drop_null_is_noop() {
    tda_al_slab_drop(nullptr);
}

// small blocks die with their chunks, so a drop with some still out leaks nothing
static void test_drop_gives_back_every_chunk() {
    tda_Al *al = tda_al_slab_new(&parent);

    for (size_t i = 0; i < 500; ++i) {
        TEST_ASSERT_NOT_NULL(tda_alloc(al, 1 + i % SMALL_MAX));
    }
    TEST_ASSERT_TRUE(tda_al_slab_stats(al).chunks > 1);

    tda_al_slab_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== small blocks ========== */

static void test_a_small_block_takes_a_chunk_once() {
    tda_Al *al = tda_al_slab_new(&parent);

    void *a = tda_alloc(al, 20);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_size_t(CHUNK, probe.last_alloc_size);

    const size_t calls = probe.alloc_calls;
    void *b = tda_alloc(al, 20);
    TEST_ASSERT_EQUAL_size_t(calls, probe.alloc_calls);

    // 20 bytes round up to the 32-byte class, carved one after another
    TEST_ASSERT_EQUAL_size_t(32, (size_t) ((unsigned char *) b - (unsigned char *) a));

    tda_dealloc(al, a, 20);
    tda_dealloc(al, b, 20);
    tda_al_slab_drop(al);
}

static void test_a_freed_block_is_handed_out_again() {
    tda_Al *al = tda_al_slab_new(&parent);

    void *a = tda_alloc(al, 40);
    tda_dealloc(al, a, 40);
    void *b = tda_alloc(al, 48); // the same class

    TEST_ASSERT_EQUAL_PTR(a, b);
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);

    tda_dealloc(al, b, 48);
    tda_al_slab_drop(al);
}

static void test_classes_do_not_share_blocks() {
    tda_Al *al = tda_al_slab_new(&parent);

    void *small = tda_alloc(al, 16);
    tda_dealloc(al, small, 16);
    void *other = tda_alloc(al, 100);

    TEST_ASSERT_NOT_EQUAL(small, other);

    tda_dealloc(al, other, 100);
    tda_al_slab_drop(al);
}

static void test_every_block_is_aligned() {
    tda_Al *al = tda_al_slab_new(&parent);

    for (size_t size = 1; size <= SMALL_MAX; size += 7) {
        void *p = tda_alloc(al, size);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) p % alignof(max_align_t));
        memset(p, 0xAB, size);
    }

    tda_al_slab_drop(al);
}

static void test_a_refused_chunk_gives_null_and_keeps_the_count() {
    tda_Al *al = tda_al_slab_new(&parent);
    tda_test_probe_fail_after_next(&probe, 0);

    TEST_ASSERT_NULL(tda_alloc(al, 16));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_slab_stats(al).small_blocks);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_slab_stats(al).chunks);

    tda_test_probe_reset(&probe);
    tda_al_slab_drop(al);
}

/* ========== large blocks ========== */

static void test_a_large_block_goes_to_the_parent_as_it_is() {
    tda_Al *al = tda_al_slab_new(&parent);

    void *p = tda_alloc(al, SMALL_MAX + 1);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(SMALL_MAX + 1, probe.last_alloc_size);
    TEST_ASSERT_EQUAL_size_t(1, tda_al_slab_stats(al).large_blocks);

    tda_dealloc(al, p, SMALL_MAX + 1);
    TEST_ASSERT_EQUAL_size_t(SMALL_MAX + 1, probe.last_dealloc_size);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_slab_stats(al).large_blocks);

    tda_al_slab_drop(al);
}

/* ========== realloc ========== */

static void test_realloc_within_a_class_stays_in_place() {
    tda_Al *al = tda_al_slab_new(&parent);

    void *p = tda_alloc(al, 33); // the 48-byte class
    void *q = tda_realloc(al, p, 33, 48);

    TEST_ASSERT_EQUAL_PTR(p, q);

    tda_dealloc(al, q, 48);
    tda_al_slab_drop(al);
}

static void test_realloc_across_classes_moves_and_keeps_the_bytes() {
    tda_Al *al = tda_al_slab_new(&parent);

    unsigned char *p = tda_alloc(al, 8);
    memcpy(p, "abcdefgh", 8);

    unsigned char *q = tda_realloc(al, p, 8, 100);
    TEST_ASSERT_NOT_EQUAL(p, q);
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", q, 8);

    unsigned char *r = tda_realloc(al, q, 100, 1000); // small to large
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", r, 8);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_slab_stats(al).small_blocks);
    TEST_ASSERT_EQUAL_size_t(1, tda_al_slab_stats(al).large_blocks);

    unsigned char *s = tda_realloc(al, r, 1000, 24); // and back
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", s, 8);
    TEST_ASSERT_EQUAL_size_t(1, tda_al_slab_stats(al).small_blocks);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_slab_stats(al).large_blocks);

    tda_dealloc(al, s, 24);
    tda_al_slab_drop(al);
}

static void test_realloc_between_large_blocks_uses_the_parents() {
    tda_Al *al = tda_al_slab_new(&parent);

    void *p = tda_alloc(al, 1000);
    const size_t before = probe.realloc_calls;
    void *q = tda_realloc(al, p, 1000, 5000);

    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(before + 1, probe.realloc_calls);
    TEST_ASSERT_EQUAL_size_t(1, tda_al_slab_stats(al).large_blocks);

    tda_dealloc(al, q, 5000);
    tda_al_slab_drop(al);
}

/* ========== containers ========== */

// every container on one slab, with the debug allocator below it checking each size the
// slab hands to its parent; the walk ends with nothing leaked
static void test_every_container_runs_on_it() {
    tda_Al *dbg = tda_al_debug_new(tda_al_default());
    tda_Al *al = tda_al_slab_new(dbg);
    TEST_ASSERT_NOT_NULL(al);

    tda_Vec *vec;
    tda_Deque *deque;
    tda_List *list;
    tda_HMap *map;
    tda_HSet *set;
    tda_PQueue *pq;
    tda_BitSet *bits;
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_vec_new(sizeof(int32_t), al, &vec));
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_deque_new(sizeof(int32_t), al, &deque));
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_list_new(sizeof(int32_t), al, &list));
    TEST_ASSERT_EQUAL(
        TDA_STATUS_OK,
        tda_hmap_new(sizeof(int32_t), sizeof(int64_t), tda_hash_i32, tda_eq_i32, al, &map)
    );
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_hset_new(sizeof(int32_t), tda_hash_i32, tda_eq_i32, al, &set));
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_pqueue_new(sizeof(int32_t), tda_cmp_i32, al, &pq));
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_bitset_new(5000, al, &bits));

    for (int32_t i = 0; i < 3000; ++i) {
        const int64_t v = i;
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_vec_push(vec, &i));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_deque_push_front(deque, &i));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_list_push_back(list, &i));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_hmap_insert(map, &i, &v, nullptr));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_hset_insert(set, &i, nullptr));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_pqueue_push(pq, &i));
        tda_bitset_set(bits, (size_t) i);
    }

    // half of each goes back, so freed blocks are handed out again on the way down
    for (int32_t i = 0; i < 3000; i += 2) {
        tda_list_pop_front(list);
        TEST_ASSERT_TRUE(tda_hmap_remove(map, &i));
        TEST_ASSERT_TRUE(tda_hset_remove(set, &i));
    }
    TEST_ASSERT_EQUAL_size_t(1500, tda_list_len(list));
    TEST_ASSERT_EQUAL_size_t(1500, tda_hmap_len(map));
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_vec_shrink_to_fit(vec));
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_hmap_shrink_to_fit(map));

    tda_vec_drop(vec);
    tda_deque_drop(deque);
    tda_list_drop(list);
    tda_hmap_drop(map);
    tda_hset_drop(set);
    tda_pqueue_drop(pq);
    tda_bitset_drop(bits);

    const tda_AlSlabStats st = tda_al_slab_stats(al);
    TEST_ASSERT_EQUAL_size_t(0, st.small_blocks);
    TEST_ASSERT_EQUAL_size_t(0, st.large_blocks);

    tda_al_slab_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_debug_stats(dbg).live_blocks);
    tda_al_debug_drop(dbg);
}

// random sizes on both sides of the small limit, each block filled with its own byte
// and checked when given back, so two live blocks that overlap cannot go unseen
static void test_random_blocks_never_overlap() {
    tda_Al *dbg = tda_al_debug_new(tda_al_default());
    tda_Al *al = tda_al_slab_new(dbg);

    enum { N = 400 };
    static unsigned char *ptrs[N];
    static size_t sizes[N];
    tda_Rng rng = tda_rng_from_seed(9);

    for (size_t step = 0; step < 20000; ++step) {
        const size_t k = tda_rng_idx(&rng, N);
        if (ptrs[k]) {
            for (size_t i = 0; i < sizes[k]; ++i) {
                TEST_ASSERT_EQUAL_UINT8((unsigned char) k, ptrs[k][i]);
            }
            tda_dealloc(al, ptrs[k], sizes[k]);
            ptrs[k] = nullptr;
        } else {
            sizes[k] = 1 + tda_rng_idx(&rng, 2 * SMALL_MAX);
            ptrs[k] = tda_alloc(al, sizes[k]);
            TEST_ASSERT_NOT_NULL(ptrs[k]);
            memset(ptrs[k], (int) (unsigned char) k, sizes[k]);
        }
    }

    for (size_t k = 0; k < N; ++k) {
        tda_dealloc(al, ptrs[k], sizes[k]);
        ptrs[k] = nullptr;
    }

    tda_al_slab_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_debug_stats(dbg).live_blocks);
    tda_al_debug_drop(dbg);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_takes_two_blocks_and_drop_returns_them);
    RUN_TEST(test_new_cleans_up_after_a_failing_parent);
    RUN_TEST(test_drop_null_is_noop);
    RUN_TEST(test_drop_gives_back_every_chunk);

    RUN_TEST(test_a_small_block_takes_a_chunk_once);
    RUN_TEST(test_a_freed_block_is_handed_out_again);
    RUN_TEST(test_classes_do_not_share_blocks);
    RUN_TEST(test_every_block_is_aligned);
    RUN_TEST(test_a_refused_chunk_gives_null_and_keeps_the_count);

    RUN_TEST(test_a_large_block_goes_to_the_parent_as_it_is);

    RUN_TEST(test_realloc_within_a_class_stays_in_place);
    RUN_TEST(test_realloc_across_classes_moves_and_keeps_the_bytes);
    RUN_TEST(test_realloc_between_large_blocks_uses_the_parents);

    RUN_TEST(test_every_container_runs_on_it);
    RUN_TEST(test_random_blocks_never_overlap);

    return UNITY_END();
}
