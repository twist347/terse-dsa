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

#include "support/layout.h"
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

static constexpr size_t CHUNK = 4096;       // mirrors CHUNK_BYTES in slab.c
static constexpr size_t CHUNK_ALIGN = 512;  // mirrors CHUNK_ALIGN
static constexpr size_t SMALL_MAX = 512;    // the largest class

// what most tests ask for: malloc's alignment, which every class gives
static tda_Layout bytes(size_t size) {
    return (tda_Layout){.size = size, .align = alignof(max_align_t)};
}

static tda_TestProbe probe;
static tda_Al parent;

void setUp() {
    tda_test_probe_reset(&probe);
    parent = tda_test_probe_full(&probe);
}

void tearDown() {
}

/* ========== lifetime ========== */

// the slab is a value the caller holds, so it takes nothing of its own from the parent
static void test_init_takes_nothing() {
    tda_Slab slab = tda_slab_init(parent);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
    TEST_ASSERT_NOT_NULL(tda_slab_to_al(&slab).vt);

    tda_slab_deinit(&slab);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_deinit_of_an_inert_slab_is_noop() {
    tda_Slab slab = {};
    tda_slab_deinit(&slab);
    tda_slab_deinit(&slab);
}

// small blocks die with their chunks, so a drop with some still out leaks nothing
static void test_deinit_gives_back_every_chunk() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    for (size_t i = 0; i < 500; ++i) {
        TEST_ASSERT_NOT_NULL(tda_alloc(al, bytes(1 + i % SMALL_MAX)));
    }
    TEST_ASSERT_TRUE(tda_slab_stats(&slab).chunks > 1);

    tda_slab_deinit(&slab);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

/* ========== small blocks ========== */

static void test_a_small_block_takes_a_chunk_once() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *a = tda_alloc(al, bytes(20));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_size_t(CHUNK, probe.last_alloc.size);
    TEST_ASSERT_EQUAL_size_t(CHUNK_ALIGN, probe.last_alloc.align);

    const size_t calls = probe.alloc_calls;
    void *b = tda_alloc(al, bytes(20));
    TEST_ASSERT_EQUAL_size_t(calls, probe.alloc_calls);

    // 20 bytes round up to the 32-byte class, carved one after another
    TEST_ASSERT_EQUAL_size_t(32, (size_t) ((unsigned char *) b - (unsigned char *) a));

    tda_dealloc(al, a, bytes(20));
    tda_dealloc(al, b, bytes(20));
    tda_slab_deinit(&slab);
}

static void test_a_freed_block_is_handed_out_again() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *a = tda_alloc(al, bytes(40));
    tda_dealloc(al, a, bytes(40));
    void *b = tda_alloc(al, bytes(48)); // the same class

    TEST_ASSERT_EQUAL_PTR(a, b);
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);

    tda_dealloc(al, b, bytes(48));
    tda_slab_deinit(&slab);
}

// the class keeps every block freed into it, not only the last: all of them come back
// before a new one is carved
static void test_every_freed_block_is_handed_out_again() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *freed[4];
    for (size_t i = 0; i < 4; ++i) {
        freed[i] = tda_alloc(al, bytes(40));
        TEST_ASSERT_NOT_NULL(freed[i]);
    }
    for (size_t i = 0; i < 4; ++i) {
        tda_dealloc(al, freed[i], bytes(40));
    }

    for (size_t i = 0; i < 4; ++i) {
        void *again = tda_alloc(al, bytes(40));

        bool seen = false;
        for (size_t j = 0; j < 4; ++j) {
            seen = seen || again == freed[j];
        }
        TEST_ASSERT_TRUE(seen);
    }

    tda_slab_deinit(&slab);
}

static void test_classes_do_not_share_blocks() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *small = tda_alloc(al, bytes(16));
    tda_dealloc(al, small, bytes(16));
    void *other = tda_alloc(al, bytes(100));

    TEST_ASSERT_NOT_EQUAL(small, other);

    tda_dealloc(al, other, bytes(100));
    tda_slab_deinit(&slab);
}

static void test_every_block_is_aligned() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    for (size_t size = 1; size <= SMALL_MAX; size += 7) {
        void *p = tda_alloc(al, bytes(size));
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) p % alignof(max_align_t));
        memset(p, 0xAB, size);
    }

    tda_slab_deinit(&slab);
}

static void test_a_refused_chunk_gives_null_and_keeps_the_count() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);
    tda_test_probe_fail_after_next(&probe, 0);

    TEST_ASSERT_NULL(tda_alloc(al, bytes(16)));
    TEST_ASSERT_EQUAL_size_t(0, tda_slab_stats(&slab).small_blocks);
    TEST_ASSERT_EQUAL_size_t(0, tda_slab_stats(&slab).chunks);

    tda_test_probe_reset(&probe);
    tda_slab_deinit(&slab);
}

// a class serves the alignments that divide its size: 20 bytes at 64 take the 64-byte
// class, at 32 the 32-byte one, and every block of either lands on its boundary
static void test_an_over_aligned_small_block_takes_a_class_it_divides() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    for (size_t align = 32; align <= SMALL_MAX; align *= 2) {
        const unsigned char *a = tda_alloc(al, tda_test_layout(20, align));
        const unsigned char *b = tda_alloc(al, tda_test_layout(20, align));
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) a % align);
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) b % align);
        TEST_ASSERT_EQUAL_size_t(align, (size_t) (b - a)); // the class is the alignment
    }
    TEST_ASSERT_EQUAL_size_t(0, tda_slab_stats(&slab).large_blocks);

    tda_slab_deinit(&slab);
}

// a class of 48 is aligned to 16 only, so a request at 32 skips it for 64
static void test_a_class_too_loosely_aligned_is_skipped() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    unsigned char *a = tda_alloc(al, tda_test_layout(40, 32));
    unsigned char *b = tda_alloc(al, tda_test_layout(40, 32));
    TEST_ASSERT_EQUAL_size_t(64, (size_t) (b - a));

    tda_dealloc(al, a, tda_test_layout(40, 32));
    tda_dealloc(al, b, tda_test_layout(40, 32));
    tda_slab_deinit(&slab);
}

/* ========== large blocks ========== */

static void test_a_large_block_goes_to_the_parent_as_it_is() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *p = tda_alloc(al, bytes(SMALL_MAX + 1));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(SMALL_MAX + 1, probe.last_alloc.size);
    TEST_ASSERT_EQUAL_size_t(1, tda_slab_stats(&slab).large_blocks);

    tda_dealloc(al, p, bytes(SMALL_MAX + 1));
    TEST_ASSERT_EQUAL_size_t(SMALL_MAX + 1, probe.last_dealloc.size);
    TEST_ASSERT_EQUAL_size_t(0, tda_slab_stats(&slab).large_blocks);

    tda_slab_deinit(&slab);
}

// an alignment past every class goes to the parent with the block, small or not
static void test_an_alignment_past_every_class_goes_to_the_parent() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *p = tda_alloc(al, tda_test_layout(16, 1024));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % 1024);
    TEST_ASSERT_EQUAL_size_t(1024, probe.last_alloc.align);
    TEST_ASSERT_EQUAL_size_t(1, tda_slab_stats(&slab).large_blocks);

    tda_dealloc(al, p, tda_test_layout(16, 1024));
    TEST_ASSERT_EQUAL_size_t(1024, probe.last_dealloc.align);

    tda_slab_deinit(&slab);
}

/* ========== realloc ========== */

static void test_resize_answers_by_the_class() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *p = tda_alloc(al, bytes(33)); // the 48-byte class
    TEST_ASSERT_TRUE(tda_resize(al, p, bytes(33), 48));
    TEST_ASSERT_TRUE(tda_resize(al, p, bytes(48), 40));
    TEST_ASSERT_FALSE(tda_resize(al, p, bytes(40), 49));
    TEST_ASSERT_FALSE(tda_resize(al, p, bytes(40), 32));

    tda_dealloc(al, p, bytes(40));
    tda_slab_deinit(&slab);
}

static void test_realloc_within_a_class_stays_in_place() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *p = tda_alloc(al, bytes(33)); // the 48-byte class
    void *q = tda_realloc(al, p, bytes(33), 48);

    TEST_ASSERT_EQUAL_PTR(p, q);

    tda_dealloc(al, q, bytes(48));
    tda_slab_deinit(&slab);
}

static void test_realloc_across_classes_moves_and_keeps_the_bytes() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    unsigned char *p = tda_alloc(al, bytes(8));
    memcpy(p, "abcdefgh", 8);

    unsigned char *q = tda_realloc(al, p, bytes(8), 100);
    TEST_ASSERT_NOT_EQUAL(p, q);
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", q, 8);

    unsigned char *r = tda_realloc(al, q, bytes(100), 1000); // small to large
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", r, 8);
    TEST_ASSERT_EQUAL_size_t(0, tda_slab_stats(&slab).small_blocks);
    TEST_ASSERT_EQUAL_size_t(1, tda_slab_stats(&slab).large_blocks);

    unsigned char *s = tda_realloc(al, r, bytes(1000), 24); // and back
    TEST_ASSERT_EQUAL_MEMORY("abcdefgh", s, 8);
    TEST_ASSERT_EQUAL_size_t(1, tda_slab_stats(&slab).small_blocks);
    TEST_ASSERT_EQUAL_size_t(0, tda_slab_stats(&slab).large_blocks);

    tda_dealloc(al, s, bytes(24));
    tda_slab_deinit(&slab);
}

static void test_realloc_between_large_blocks_uses_the_parents() {
    tda_Slab slab = tda_slab_init(parent);
    tda_Al al = tda_slab_to_al(&slab);

    void *p = tda_alloc(al, bytes(1000));
    const size_t before = probe.remap_calls;
    void *q = tda_realloc(al, p, bytes(1000), 5000);

    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(before + 1, probe.remap_calls);
    TEST_ASSERT_EQUAL_size_t(1, tda_slab_stats(&slab).large_blocks);

    tda_dealloc(al, q, bytes(5000));
    tda_slab_deinit(&slab);
}

/* ========== containers ========== */

// every container on one slab, with the debug allocator below it checking each size the
// slab hands to its parent; the walk ends with nothing leaked
static void test_every_container_runs_on_it() {
    tda_Debug dbg = tda_debug_init(tda_al_default());
    tda_Slab slab = tda_slab_init(tda_debug_to_al(&dbg));
    tda_Al al = tda_slab_to_al(&slab);

    tda_Vec vec = tda_vec_init(TDA_LAYOUT_OF(int32_t), al);
    tda_Deque deque = tda_deque_init(TDA_LAYOUT_OF(int32_t), al);
    tda_List list = tda_list_init(TDA_LAYOUT_OF(int32_t), al);
    tda_HMap map = tda_hmap_init(TDA_LAYOUT_OF(int32_t), TDA_LAYOUT_OF(int64_t), tda_hash_i32, tda_eq_i32, nullptr, al);
    tda_HSet set = tda_hset_init(TDA_LAYOUT_OF(int32_t), tda_hash_i32, tda_eq_i32, nullptr, al);
    tda_PQueue pq = tda_pqueue_init(TDA_LAYOUT_OF(int32_t), tda_cmp_i32, nullptr, al);
    tda_BitSet bits = {};
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_bitset_init_nbits(5000, al, &bits));

    for (int32_t i = 0; i < 3000; ++i) {
        const int64_t v = i;
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_vec_push(&vec, &i));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_deque_push_front(&deque, &i));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_list_push_back(&list, &i));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_hmap_insert(&map, &i, &v, nullptr));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_hset_insert(&set, &i, nullptr));
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_pqueue_push(&pq, &i));
        tda_bitset_set(&bits, (size_t) i);
    }

    // half of each goes back, so freed blocks are handed out again on the way down
    for (int32_t i = 0; i < 3000; i += 2) {
        tda_list_pop_front(&list);
        TEST_ASSERT_TRUE(tda_hmap_remove(&map, &i));
        TEST_ASSERT_TRUE(tda_hset_remove(&set, &i));
    }
    TEST_ASSERT_EQUAL_size_t(1500, tda_list_len(&list));
    TEST_ASSERT_EQUAL_size_t(1500, tda_hmap_len(&map));
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_vec_shrink_to_fit(&vec));
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_hmap_shrink_to_fit(&map));

    tda_vec_deinit(&vec);
    tda_deque_deinit(&deque);
    tda_list_deinit(&list);
    tda_hmap_deinit(&map);
    tda_hset_deinit(&set);
    tda_pqueue_deinit(&pq);
    tda_bitset_deinit(&bits);

    const tda_SlabStats st = tda_slab_stats(&slab);
    TEST_ASSERT_EQUAL_size_t(0, st.small_blocks);
    TEST_ASSERT_EQUAL_size_t(0, st.large_blocks);

    tda_slab_deinit(&slab);
    TEST_ASSERT_EQUAL_size_t(0, tda_debug_stats(&dbg).live_blocks);
    tda_debug_deinit(&dbg);
}

// random sizes on both sides of the small limit and random alignments, each block filled
// with its own byte
// and checked when given back, so two live blocks that overlap cannot go unseen
static void test_random_blocks_never_overlap() {
    tda_Debug dbg = tda_debug_init(tda_al_default());
    tda_Slab slab = tda_slab_init(tda_debug_to_al(&dbg));
    tda_Al al = tda_slab_to_al(&slab);

    enum { N = 400 };
    static unsigned char *ptrs[N];
    static tda_Layout layouts[N];
    tda_Rng rng = tda_rng_from_seed(9);

    for (size_t step = 0; step < 20000; ++step) {
        const size_t k = tda_rng_idx(&rng, N);
        if (ptrs[k]) {
            for (size_t i = 0; i < layouts[k].size; ++i) {
                TEST_ASSERT_EQUAL_UINT8((unsigned char) k, ptrs[k][i]);
            }
            tda_dealloc(al, ptrs[k], layouts[k]);
            ptrs[k] = nullptr;
        } else {
            // alignments from 1 to 1024, past every class
            layouts[k] = tda_test_layout(1 + tda_rng_idx(&rng, 2 * SMALL_MAX), (size_t) 1 << tda_rng_idx(&rng, 11));
            ptrs[k] = tda_alloc(al, layouts[k]);
            TEST_ASSERT_NOT_NULL(ptrs[k]);
            TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) ptrs[k] % layouts[k].align);
            memset(ptrs[k], (int) (unsigned char) k, layouts[k].size);
        }
    }

    for (size_t k = 0; k < N; ++k) {
        tda_dealloc(al, ptrs[k], layouts[k]);
        ptrs[k] = nullptr;
    }

    tda_slab_deinit(&slab);
    TEST_ASSERT_EQUAL_size_t(0, tda_debug_stats(&dbg).live_blocks);
    tda_debug_deinit(&dbg);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_Slab x = tda_slab_init(tda_al_default());
    void *p = tda_alloc(tda_slab_to_al(&x), (tda_Layout){.size = 16, .align = 8});
    TEST_ASSERT_NOT_NULL(p);
    tda_dealloc(tda_slab_to_al(&x), p, (tda_Layout){.size = 16, .align = 8});

    tda_slab_deinit(&x);
    TEST_ASSERT_NULL(x.parent_.vt);

    tda_slab_deinit(&x);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_takes_nothing);
    RUN_TEST(test_deinit_of_an_inert_slab_is_noop);
    RUN_TEST(test_deinit_gives_back_every_chunk);

    RUN_TEST(test_a_small_block_takes_a_chunk_once);
    RUN_TEST(test_a_freed_block_is_handed_out_again);
    RUN_TEST(test_every_freed_block_is_handed_out_again);
    RUN_TEST(test_classes_do_not_share_blocks);
    RUN_TEST(test_every_block_is_aligned);
    RUN_TEST(test_a_refused_chunk_gives_null_and_keeps_the_count);
    RUN_TEST(test_an_over_aligned_small_block_takes_a_class_it_divides);
    RUN_TEST(test_a_class_too_loosely_aligned_is_skipped);

    RUN_TEST(test_a_large_block_goes_to_the_parent_as_it_is);
    RUN_TEST(test_an_alignment_past_every_class_goes_to_the_parent);

    RUN_TEST(test_resize_answers_by_the_class);

    RUN_TEST(test_realloc_within_a_class_stays_in_place);
    RUN_TEST(test_realloc_across_classes_moves_and_keeps_the_bytes);
    RUN_TEST(test_realloc_between_large_blocks_uses_the_parents);

    RUN_TEST(test_every_container_runs_on_it);
    RUN_TEST(test_random_blocks_never_overlap);

    RUN_TEST(test_deinit_twice_is_noop);

    return UNITY_END();
}
