// Every container, built on the layout of an over-aligned type, hands out elems on that
// alignment: from the first push through every growth, in a buffer or in a node, on each
// kind of allocator. Before 2.0 an elem past max_align_t sat wherever malloc put it.

#include "tda/alloc/arena.h"
#include "tda/alloc/debug.h"
#include "tda/alloc/default.h"
#include "tda/alloc/pool.h"
#include "tda/alloc/slab.h"
#include "tda/algo/merge.h"
#include "tda/algo/permute.h"
#include "tda/algo/sort.h"
#include "tda/core/hash.h"
#include "tda/ds/arr.h"
#include "tda/ds/deque.h"
#include "tda/ds/hmap.h"
#include "tda/ds/hset.h"
#include "tda/ds/list.h"
#include "tda/ds/pqueue.h"
#include "tda/ds/queue.h"
#include "tda/ds/stack.h"
#include "tda/ds/vec.h"

#include "support/probe.h"
#include "support/status.h"

#include <unity.h>

#include <stdint.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

// a SIMD lane: past every malloc's own alignment, and a size its alignment divides
typedef struct {
    alignas(64) int32_t id;
    float pad[15];
} Wide;

static_assert(alignof(Wide) == 64 && sizeof(Wide) == 64);

// enough to make every growable container grow several times
static constexpr int32_t N = 300;

static bool is_wide_aligned(const void *ptr) {
    return (uintptr_t) ptr % alignof(Wide) == 0;
}

static Wide wide(int32_t id) {
    return (Wide){.id = id};
}

static int cmp_wide(const void *lhs, const void *rhs, void *) {
    const int32_t a = ((const Wide *) lhs)->id, b = ((const Wide *) rhs)->id;

    return (a > b) - (a < b);
}

static tda_Hash hash_wide(const void *key, void *) {
    return tda_hash_i32(&((const Wide *) key)->id, nullptr);
}

static bool eq_wide(const void *lhs, const void *rhs, void *) {
    return ((const Wide *) lhs)->id == ((const Wide *) rhs)->id;
}

/* ========== one container at a time, over one allocator ========== */

static void check_arr(tda_Al al) {
    tda_Arr a = {};
    TDA_TEST_OK(TDA_ARR_INIT_LEN(Wide, N, al, &a));
    TEST_ASSERT_TRUE(is_wide_aligned(tda_arr_data(&a)));
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_arr_elem_layout(&a)));
    tda_arr_deinit(&a);
}

static void check_vec(tda_Al al) {
    tda_Vec v = TDA_VEC_INIT(Wide, al);
    for (int32_t i = 0; i < N; ++i) {
        const Wide w = wide(i);
        TDA_TEST_OK(tda_vec_push(&v, &w));
        TEST_ASSERT_TRUE(is_wide_aligned(tda_vec_data(&v)));
    }
    TEST_ASSERT_EQUAL_INT32(N - 1, TDA_VEC_BACK_AS(Wide, &v)->id);
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_vec_elem_layout(&v)));
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_vec_to_span(&v).elem));
    TDA_TEST_OK(tda_vec_shrink_to_fit(&v));
    TEST_ASSERT_TRUE(is_wide_aligned(tda_vec_data(&v)));
    tda_vec_deinit(&v);
}

static void check_deque(tda_Al al) {
    tda_Deque d = TDA_DEQUE_INIT(Wide, al);
    for (int32_t i = 0; i < N; ++i) {
        const Wide w = wide(i);
        TDA_TEST_OK(i % 2 ? tda_deque_push_back(&d, &w) : tda_deque_push_front(&d, &w));
    }
    for (size_t i = 0; i < tda_deque_len(&d); ++i) {
        TEST_ASSERT_TRUE(is_wide_aligned(tda_deque_get(&d, i)));
    }
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_deque_elem_layout(&d)));
    tda_deque_deinit(&d);
}

static void check_list(tda_Al al) {
    tda_List l = TDA_LIST_INIT(Wide, al);
    for (int32_t i = 0; i < N; ++i) {
        const Wide w = wide(i);
        TDA_TEST_OK(tda_list_push_back(&l, &w));
    }
    int32_t want = 0;
    TDA_LIST_FOR_EACH (node, &l) {
        const Wide *w = TDA_LIST_NODE_ELEM_AS(Wide, &l, node);
        TEST_ASSERT_TRUE(is_wide_aligned(w));
        TEST_ASSERT_EQUAL_INT32(want++, w->id);
    }
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_list_elem_layout(&l)));
    tda_list_deinit(&l);
}

// a narrow key and a wide value: the value has to skip to its boundary inside the node
static void check_hmap(tda_Al al) {
    tda_HMap m = TDA_HMAP_INIT(int32_t, Wide, tda_hash_i32, tda_eq_i32, nullptr, al);
    for (int32_t i = 0; i < N; ++i) {
        const Wide w = wide(i);
        TDA_TEST_OK(tda_hmap_insert(&m, &i, &w, nullptr));
    }
    for (int32_t i = 0; i < N; ++i) {
        const Wide *w = tda_hmap_get(&m, &i);
        TEST_ASSERT_TRUE(is_wide_aligned(w));
        TEST_ASSERT_EQUAL_INT32(i, w->id);
    }
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(int32_t), tda_hmap_key_layout(&m)));
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_hmap_val_layout(&m)));
    tda_hmap_deinit(&m);
}

static void check_hset(tda_Al al) {
    tda_HSet s = TDA_HSET_INIT(Wide, hash_wide, eq_wide, nullptr, al);
    for (int32_t i = 0; i < N; ++i) {
        const Wide w = wide(i);
        TDA_TEST_OK(tda_hset_insert(&s, &w, nullptr));
    }
    TDA_HSET_FOR_EACH (node, &s) {
        TEST_ASSERT_TRUE(is_wide_aligned(tda_hset_node_key(&s, node)));
    }
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_hset_key_layout(&s)));
    tda_hset_deinit(&s);
}

static void check_adapters(tda_Al al) {
    tda_Stack st = TDA_STACK_INIT(Wide, al);
    tda_Queue q = TDA_QUEUE_INIT(Wide, al);
    tda_PQueue pq = TDA_PQUEUE_INIT(Wide, cmp_wide, nullptr, al);
    for (int32_t i = 0; i < N; ++i) {
        const Wide w = wide(i);
        TDA_TEST_OK(tda_stack_push(&st, &w));
        TDA_TEST_OK(tda_queue_push(&q, &w));
        TDA_TEST_OK(tda_pqueue_push(&pq, &w));
        TEST_ASSERT_TRUE(is_wide_aligned(tda_stack_top(&st)));
        TEST_ASSERT_TRUE(is_wide_aligned(tda_queue_back(&q)));
        TEST_ASSERT_TRUE(is_wide_aligned(tda_pqueue_top(&pq)));
    }
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_stack_elem_layout(&st)));
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_queue_elem_layout(&q)));
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Wide), tda_pqueue_elem_layout(&pq)));
    tda_stack_deinit(&st);
    tda_queue_deinit(&q);
    tda_pqueue_deinit(&pq);
}

static void check_all(tda_Al al) {
    check_arr(al);
    check_vec(al);
    check_deque(al);
    check_list(al);
    check_hmap(al);
    check_hset(al);
    check_adapters(al);
}

/* ========== tests ========== */

static void test_every_container_aligns_its_elems_on_default() {
    check_all(tda_al_default());
}

// the arena packs blocks back to back, so an elem lands aligned only if it is asked for
static void test_every_container_aligns_its_elems_on_an_arena() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4 << 20, tda_al_default(), &arena));

    check_all(tda_arena_to_al(&arena));

    tda_arena_deinit(&arena);
}

// nodes and small buffers come from classes, large buffers from the parent
static void test_every_container_aligns_its_elems_on_a_slab() {
    tda_Slab slab = tda_slab_init(tda_al_default());

    check_all(tda_slab_to_al(&slab));

    TEST_ASSERT_EQUAL_size_t(0, tda_slab_stats(&slab).small_blocks);
    TEST_ASSERT_EQUAL_size_t(0, tda_slab_stats(&slab).large_blocks);
    tda_slab_deinit(&slab);
}

// the debug allocator stops on any block given back with another size or alignment than
// it was made with, so every path that frees, grows or moves an elem block is checked
static void test_every_container_gives_back_what_it_took_on_debug() {
    tda_Debug debug = tda_debug_init(tda_al_default());

    check_all(tda_debug_to_al(&debug));

    TEST_ASSERT_EQUAL_size_t(0, tda_debug_stats(&debug).live_blocks);
    tda_debug_deinit(&debug);
}

// a list of wide elems on a pool cut by node_layout: every node fits a block
static void test_a_list_of_wide_elems_runs_on_a_pool_of_its_node_layout() {
    constexpr size_t count = 16;
    tda_Pool pool;
    TDA_TEST_OK(tda_pool_init_cap(count, tda_list_node_layout(TDA_LAYOUT_OF(Wide)), tda_al_default(), &pool));

    tda_List l = TDA_LIST_INIT(Wide, tda_pool_to_al(&pool));
    for (int32_t i = 0; i + 1 < (int32_t) count; ++i) {
        const Wide w = wide(i);
        TDA_TEST_OK(tda_list_push_back(&l, &w));
    }
    TDA_LIST_FOR_EACH (node, &l) {
        TEST_ASSERT_TRUE(is_wide_aligned(TDA_LIST_NODE_ELEM_AS(Wide, &l, node)));
    }

    tda_list_deinit(&l);
    TEST_ASSERT_EQUAL_size_t(count, tda_pool_stats(&pool).free);
    tda_pool_deinit(&pool);
}

// a comparator that checks its operands sit on their boundary, scratch copies included
static int cmp_wide_aligned(const void *lhs, const void *rhs, void *) {
    TEST_ASSERT_TRUE(is_wide_aligned(lhs));
    TEST_ASSERT_TRUE(is_wide_aligned(rhs));

    return cmp_wide(lhs, rhs, nullptr);
}

static bool is_even_wide(const void *elem, void *) {
    return ((const Wide *) elem)->id % 2 == 0;
}

// the algos that copy elems aside ask for the copy in the span's layout, and the
// comparator sees the copies on their boundary: the probe records each request, and an
// arena knocked off the grid before each call aligns to exactly what it is asked
static void test_algo_scratch_buffers_take_the_elems_layout() {
    static Wide buf[64];
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(Wide, buf, 64);
    tda_TestProbe probe;
    tda_test_probe_reset(&probe);
    tda_Al probe_al = tda_test_probe_bare(&probe);

    for (int round = 0; round < 2; ++round) {
        const bool on_arena = round == 1;
        tda_Al al = probe_al;
        tda_Arena arena_v = {};
        tda_Al arena = {};
        if (on_arena) {
            TDA_TEST_OK(tda_arena_init_cap(1 << 20, tda_al_default(), &arena_v));
            arena = tda_arena_to_al(&arena_v);
            al = arena;
        }

        for (int32_t i = 0; i < 64; ++i) {
            buf[i] = wide((i * 37) % 64);
        }
        if (on_arena) {
            TEST_ASSERT_NOT_NULL(tda_alloc(arena, (tda_Layout){.size = 1, .align = 1}));
        }
        TDA_TEST_OK(tda_span_sort_stable(s, cmp_wide_aligned, nullptr, al));
        if (!on_arena) {
            TEST_ASSERT_EQUAL_size_t(alignof(Wide), probe.last_alloc.align);
        }
        for (int32_t i = 0; i < 64; ++i) {
            TEST_ASSERT_EQUAL_INT32(i, buf[i].id);
        }

        for (int32_t i = 0; i < 32; ++i) {
            buf[i] = wide(2 * i);
            buf[32 + i] = wide(2 * i + 1);
        }
        if (on_arena) {
            TEST_ASSERT_NOT_NULL(tda_alloc(arena, (tda_Layout){.size = 1, .align = 1}));
        }
        tda_span_inplace_merge(s, 32, cmp_wide_aligned, nullptr, al);
        if (!on_arena) {
            TEST_ASSERT_EQUAL_size_t(alignof(Wide), probe.last_alloc.align);
        }
        for (int32_t i = 0; i < 64; ++i) {
            TEST_ASSERT_EQUAL_INT32(i, buf[i].id);
        }

        if (on_arena) {
            TEST_ASSERT_NOT_NULL(tda_alloc(arena, (tda_Layout){.size = 1, .align = 1}));
        }
        size_t boundary;
        TDA_TEST_OK(tda_span_partition_stable(s, is_even_wide, nullptr, al, &boundary));
        if (!on_arena) {
            TEST_ASSERT_EQUAL_size_t(alignof(Wide), probe.last_alloc.align);
        }
        TEST_ASSERT_EQUAL_size_t(32, boundary);

        tda_arena_deinit(&arena_v);
    }

    TEST_ASSERT_EQUAL_size_t(3, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a loosely aligned elem asks only its own alignment for a copy, so a pool of narrow
// blocks serves it; a scratch alignment read off the size would ask 16 and be refused
static void test_a_pool_of_narrow_blocks_serves_a_stable_sort() {
    typedef struct {
        int32_t key, a, b, c;
    } Quad;
    static_assert(alignof(Quad) == 4 && sizeof(Quad) == 16);

    tda_Pool pool;
    TDA_TEST_OK(tda_pool_init_cap(2, (tda_Layout){.size = 1024, .align = 4}, tda_al_default(), &pool));

    Quad buf[8];
    for (int32_t i = 0; i < 8; ++i) {
        buf[i] = (Quad){.key = 7 - i};
    }
    TDA_TEST_OK(tda_span_sort_stable(TDA_SPAN_FROM_DATA_MUT(Quad, buf, 8), tda_cmp_i32, nullptr, tda_pool_to_al(&pool)));
    TEST_ASSERT_EQUAL_INT32(0, buf[0].key);

    tda_pool_deinit(&pool);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_every_container_aligns_its_elems_on_default);
    RUN_TEST(test_every_container_aligns_its_elems_on_an_arena);
    RUN_TEST(test_every_container_aligns_its_elems_on_a_slab);
    RUN_TEST(test_every_container_gives_back_what_it_took_on_debug);
    RUN_TEST(test_a_list_of_wide_elems_runs_on_a_pool_of_its_node_layout);
    RUN_TEST(test_algo_scratch_buffers_take_the_elems_layout);
    RUN_TEST(test_a_pool_of_narrow_blocks_serves_a_stable_sort);

    return UNITY_END();
}
