#include "tda/alloc/pool.h"
#include "tda/alloc/default.h"

#include "support/probe.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// what most tests build their pools at; a block is rounded up to its alignment and must
// also hold a free-list pointer
static constexpr size_t ALIGNMENT = alignof(max_align_t);

static size_t aligned(size_t size) {
    if (size < sizeof(void *)) {
        size = sizeof(void *);
    }

    return (size + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
}

// most tests build and ask at malloc's alignment, so the sizes round as aligned() does
static tda_Layout block(size_t size) {
    return (tda_Layout){.size = size, .align = ALIGNMENT};
}

/* ========== probe allocator ==========
 *
 * The shared probe, bare: counts live blocks and can fail on demand, so the pool's
 * construction and teardown paths can be checked for leaks.
 */

static tda_TestProbe probe;

static tda_Al probe_al() {
    return tda_test_probe_bare(&probe);
}

void setUp() {
    tda_test_probe_reset(&probe);
}

void tearDown() {
}

/* ========== lifetime ========== */

static void test_new_starts_with_every_block_free() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 4);
    TEST_ASSERT_NOT_NULL(pool);

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(32, st.block_size);
    TEST_ASSERT_EQUAL_size_t(4, st.block_count);
    TEST_ASSERT_EQUAL_size_t(0, st.used);
    TEST_ASSERT_EQUAL_size_t(4, st.free);

    tda_al_pool_drop(pool);
}

// a block too small to hold the free-list pointer is grown, then aligned up
static void test_new_raises_a_tiny_block_size() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(1), 4);
    TEST_ASSERT_NOT_NULL(pool);

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(aligned(1), st.block_size);
    TEST_ASSERT_TRUE(st.block_size >= sizeof(void *));

    tda_al_pool_drop(pool);
}

static void test_new_rounds_the_block_size_up() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(ALIGNMENT + 1), 2);
    TEST_ASSERT_NOT_NULL(pool);

    TEST_ASSERT_EQUAL_size_t(2 * ALIGNMENT, tda_al_pool_stats(pool).block_size);

    tda_al_pool_drop(pool);
}

// a layout of its own alignment is how a pool holds an over-aligned type, and a block
// of it never takes more than one stride
static void test_new_takes_the_alignment_from_the_layout() {
    typedef struct {
        alignas(64) float lane[4];
    } Wide;

    tda_Al *pool = tda_al_pool_new(tda_al_default(), TDA_LAYOUT_OF(Wide), 5);
    TEST_ASSERT_NOT_NULL(pool);

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(64, st.block_size);
    TEST_ASSERT_EQUAL_size_t(64, st.block_align);

    for (size_t i = 0; i < 5; ++i) {
        Wide *w = TDA_ALLOC(Wide, pool, 1);
        TEST_ASSERT_NOT_NULL(w);
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) w % 64);
    }

    tda_al_pool_drop(pool);
}

// a small type gets the free list's pointer as its floor, not malloc's alignment
static void test_new_of_a_small_type_takes_a_pointer_per_block() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), TDA_LAYOUT_OF(int32_t), 4);
    TEST_ASSERT_NOT_NULL(pool);

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(sizeof(void *), st.block_size);
    TEST_ASSERT_EQUAL_size_t(alignof(void *), st.block_align);

    // a request below the blocks' alignment is served as is
    TEST_ASSERT_NOT_NULL(TDA_ALLOC(char, pool, 3));

    tda_al_pool_drop(pool);
}

// the pool borrows its parent: everything it took must go back on drop
static void test_drop_returns_everything_to_the_parent() {
    tda_Al parent = probe_al();

    tda_Al *pool = tda_al_pool_new(&parent, block(32), 4);
    TEST_ASSERT_NOT_NULL(pool);
    TEST_ASSERT_EQUAL_size_t(3, probe.live); // context, buffer, the tda_Al itself

    tda_al_pool_drop(pool);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// each construction step can fail; none of them may leak the earlier ones
static void test_new_cleans_up_after_a_failing_parent() {
    for (size_t fail_at = 0; fail_at < 3; ++fail_at) {
        tda_test_probe_reset(&probe);
        tda_test_probe_fail_after_next(&probe, fail_at);
        tda_Al parent = probe_al();

        TEST_ASSERT_NULL(tda_al_pool_new(&parent, block(32), 4));
        TEST_ASSERT_EQUAL_size_t(0, probe.live);
    }
}

static void test_drop_null_is_noop() {
    tda_al_pool_drop(nullptr);
}

/* ========== alloc ========== */

static void test_alloc_takes_one_block() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 4);

    TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(32)));

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(1, st.used);
    TEST_ASSERT_EQUAL_size_t(3, st.free);

    tda_al_pool_drop(pool);
}

// a request smaller than a block still consumes a whole block
static void test_alloc_of_a_partial_block_still_costs_one() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(64), 2);

    TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(1)));
    TEST_ASSERT_EQUAL_size_t(1, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

// the pool hands out fixed slots, so anything larger cannot be served
static void test_alloc_larger_than_a_block_fails() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 4);

    TEST_ASSERT_NULL(tda_alloc(pool, block(33)));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

static void test_alloc_zero_is_null_and_costs_nothing() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 4);

    TEST_ASSERT_NULL(tda_alloc(pool, block(0)));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

static void test_exhaustion_returns_null() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 3);

    for (size_t i = 0; i < 3; ++i) {
        TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(32)));
    }

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(3, st.used);
    TEST_ASSERT_EQUAL_size_t(0, st.free);

    TEST_ASSERT_NULL(tda_alloc(pool, block(32)));
    TEST_ASSERT_EQUAL_size_t(3, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

static void test_every_block_is_aligned() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(24), 4);

    for (size_t i = 0; i < 4; ++i) {
        void *p = tda_alloc(pool, block(24));
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) p % ALIGNMENT);
    }

    tda_al_pool_drop(pool);
}

// every block must be a distinct, non-overlapping region of the backing buffer
static void test_blocks_do_not_overlap() {
    constexpr size_t COUNT = 4;
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), COUNT);

    unsigned char *blocks[COUNT];
    for (size_t i = 0; i < COUNT; ++i) {
        blocks[i] = tda_alloc(pool, block(32));
        TEST_ASSERT_NOT_NULL(blocks[i]);
        memset(blocks[i], (int) (i + 1), 32);
    }

    // if the slots overlapped, an earlier stamp would have been overwritten
    for (size_t i = 0; i < COUNT; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), blocks[i][0]);
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), blocks[i][31]);
    }

    tda_al_pool_drop(pool);
}

/* ========== dealloc ========== */

static void test_dealloc_returns_the_block() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 2);

    void *p = tda_alloc(pool, block(32));
    TEST_ASSERT_EQUAL_size_t(1, tda_al_pool_stats(pool).used);

    tda_dealloc(pool, p, block(32));

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(0, st.used);
    TEST_ASSERT_EQUAL_size_t(2, st.free);

    tda_al_pool_drop(pool);
}

// the free list is LIFO: the block just returned is the next one handed out
static void test_freed_block_is_reused_first() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 3);

    void *a = tda_alloc(pool, block(32));
    void *b = tda_alloc(pool, block(32));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    tda_dealloc(pool, b, block(32));
    TEST_ASSERT_EQUAL_PTR(b, tda_alloc(pool, block(32)));

    tda_al_pool_drop(pool);
}

// the double-free check looks at the head of the free list; a block taken back from there
// belongs to its new owner, so freeing it again is an ordinary free
static void test_block_taken_back_from_the_head_frees_again() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 3);

    void *a = tda_alloc(pool, block(32));
    TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(32)));

    tda_dealloc(pool, a, block(32));
    TEST_ASSERT_EQUAL_PTR(a, tda_alloc(pool, block(32)));
    tda_dealloc(pool, a, block(32));

    TEST_ASSERT_EQUAL_size_t(1, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

// a fully drained pool becomes usable again once blocks come back
static void test_exhausted_pool_recovers_after_a_free() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 2);

    void *a = tda_alloc(pool, block(32));
    TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(32)));
    TEST_ASSERT_NULL(tda_alloc(pool, block(32)));

    tda_dealloc(pool, a, block(32));
    TEST_ASSERT_EQUAL_PTR(a, tda_alloc(pool, block(32)));

    tda_al_pool_drop(pool);
}

static void test_dealloc_null_is_noop() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 2);

    TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(32)));
    tda_dealloc(pool, nullptr, block(32));
    TEST_ASSERT_EQUAL_size_t(1, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

/* ========== alloc_zeroed ========== */

static void test_alloc_zeroed_zeroes_the_block() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 2);

    const unsigned char *p = tda_alloc_zeroed(pool, block(8 * 4));
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_al_pool_drop(pool);
}

// a recycled block must not leak the previous tenant's bytes through alloc_zeroed
static void test_alloc_zeroed_zeroes_a_recycled_block() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 2);

    unsigned char *first = tda_alloc(pool, block(32));
    TEST_ASSERT_NOT_NULL(first);
    memset(first, 0xFF, 32);
    tda_dealloc(pool, first, block(32));

    const unsigned char *second = tda_alloc_zeroed(pool, block(8 * 4));
    TEST_ASSERT_EQUAL_PTR(first, second);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, second[i]);
    }

    tda_al_pool_drop(pool);
}

static void test_alloc_zeroed_larger_than_a_block_fails() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 2);

    TEST_ASSERT_NULL(tda_alloc_zeroed(pool, block(8 * 8)));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

static void test_alloc_zeroed_rejects_overflow() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 2);

    TEST_ASSERT_NULL(TDA_ALLOC_ZEROED(int32_t, pool, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

/* ========== realloc ========== */

// every block has the pool's one size, so a new size that fits keeps it in place
static void test_realloc_within_a_block_stays_in_place() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 3);

    unsigned char *p = tda_alloc(pool, block(16));
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 16; ++i) {
        p[i] = (unsigned char) (i + 1);
    }

    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(pool, p, block(16), 32));
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), p[i]);
    }
    TEST_ASSERT_EQUAL_size_t(1, tda_al_pool_stats(pool).used);

    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(pool, p, block(32), 8));

    tda_al_pool_drop(pool);
}

// in place needs no second block, so a full pool can still resize
static void test_realloc_in_place_needs_no_free_block() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 1);

    void *p = tda_alloc(pool, block(8));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_pool_stats(pool).free);

    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(pool, p, block(8), 32));

    tda_al_pool_drop(pool);
}

// more than a block is more than the pool can ever give; the old block stays the caller's
static void test_realloc_beyond_a_block_fails_and_keeps_the_block() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 3);

    unsigned char *p = tda_alloc(pool, block(16));
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x5A, 16);

    TEST_ASSERT_NULL(tda_realloc(pool, p, block(16), 33));
    TEST_ASSERT_EQUAL_UINT8(0x5A, p[15]);
    TEST_ASSERT_EQUAL_size_t(1, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

static void test_realloc_of_null_is_an_alloc() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 3);

    TEST_ASSERT_NOT_NULL(tda_realloc(pool, nullptr, block(0), 16));
    TEST_ASSERT_EQUAL_size_t(1, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

/* ========== reset ========== */

static void test_reset_frees_every_block() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 3);

    void *first = tda_alloc(pool, block(32));
    TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(32)));
    TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(32)));

    tda_al_pool_reset(pool);

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(0, st.used);
    TEST_ASSERT_EQUAL_size_t(3, st.free);

    // the free list is rebuilt in order, so the first block comes back first
    TEST_ASSERT_EQUAL_PTR(first, tda_alloc(pool, block(32)));

    tda_al_pool_drop(pool);
}

// reset after partial frees must rebuild the list, not append to a stale one
static void test_reset_recovers_from_a_scrambled_free_list() {
    tda_Al *pool = tda_al_pool_new(tda_al_default(), block(32), 3);

    void *a = tda_alloc(pool, block(32));
    void *b = tda_alloc(pool, block(32));
    tda_dealloc(pool, a, block(32));
    tda_dealloc(pool, b, block(32));

    tda_al_pool_reset(pool);
    TEST_ASSERT_EQUAL_size_t(3, tda_al_pool_stats(pool).free);

    // all three blocks must still be reachable, and all distinct
    void *got[3];
    for (size_t i = 0; i < 3; ++i) {
        got[i] = tda_alloc(pool, block(32));
        TEST_ASSERT_NOT_NULL(got[i]);
    }
    TEST_ASSERT_TRUE(got[0] != got[1]);
    TEST_ASSERT_TRUE(got[1] != got[2]);
    TEST_ASSERT_TRUE(got[0] != got[2]);

    tda_al_pool_drop(pool);
}

/* ========== from_buf ========== */

// the memory the tests build in; its size is well above any header
static alignas(max_align_t) unsigned char mem[512];

static bool inside(const void *ptr, size_t size, const void *buf, size_t buf_size) {
    const uintptr_t addr = (uintptr_t) ptr;
    const uintptr_t begin = (uintptr_t) buf;

    return addr >= begin && addr + size <= begin + buf_size;
}

// the header is paid for out of the buffer, and the rest is cut into as
// many blocks as fit
static void test_from_buf_fits_as_many_blocks_as_the_rest_holds() {
    tda_Al *pool = tda_al_pool_from_buf(mem, sizeof(mem), block(32));
    TEST_ASSERT_NOT_NULL(pool);
    TEST_ASSERT_TRUE(inside(pool, sizeof(tda_Al), mem, sizeof(mem)));

    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    TEST_ASSERT_EQUAL_size_t(aligned(32), st.block_size);
    TEST_ASSERT_TRUE(st.block_count > 0);
    TEST_ASSERT_TRUE(st.block_count * st.block_size < sizeof(mem));

    for (size_t i = 0; i < st.block_count; ++i) {
        void *p = tda_alloc(pool, block(32));
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_TRUE(inside(p, 32, mem, sizeof(mem)));
    }
    TEST_ASSERT_NULL(tda_alloc(pool, block(32)));

    tda_al_pool_drop(pool);
}

// the blocks run up to the end of the buffer: what is left past the last one is too short
// for another. Every size over one block's span puts the end exactly on a block once
static void test_from_buf_wastes_less_than_a_block_at_the_end() {
    const size_t span = aligned(32);

    for (size_t size = sizeof(mem) - span; size < sizeof(mem); ++size) {
        tda_Al *pool = tda_al_pool_from_buf(mem, size, block(32));
        TEST_ASSERT_NOT_NULL(pool);

        uintptr_t end = 0;
        for (void *p; (p = tda_alloc(pool, block(32)));) {
            if ((uintptr_t) p + span > end) {
                end = (uintptr_t) p + span;
            }
        }
        TEST_ASSERT_TRUE((uintptr_t) mem + size - end < span);

        tda_al_pool_drop(pool);
    }
}

// the caller's array may sit at any address; every block still has to be aligned
static void test_from_buf_aligns_whatever_the_buffer() {
    for (size_t off = 0; off < ALIGNMENT; ++off) {
        tda_Al *pool = tda_al_pool_from_buf(mem + off, sizeof(mem) - off, block(24));
        TEST_ASSERT_NOT_NULL(pool);

        for (void *p; (p = tda_alloc(pool, block(24)));) {
            TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) p % ALIGNMENT);
            TEST_ASSERT_TRUE(inside(p, 24, mem + off, sizeof(mem) - off));
        }

        tda_al_pool_drop(pool);
    }
}

// blocks wider than the header's alignment start past it, on their own
static void test_from_buf_aligns_to_the_blocks_own_alignment() {
    for (size_t off = 0; off < 64; off += 8) {
        tda_Al *pool = tda_al_pool_from_buf(mem + off, sizeof(mem) - off, (tda_Layout){.size = 64, .align = 64});
        TEST_ASSERT_NOT_NULL(pool);
        TEST_ASSERT_EQUAL_size_t(64, tda_al_pool_stats(pool).block_align);

        size_t n = 0;
        for (void *p; (p = tda_alloc(pool, (tda_Layout){.size = 64, .align = 64})); ++n) {
            TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % 64);
            TEST_ASSERT_TRUE((uintptr_t) p + 64 <= (uintptr_t) mem + sizeof(mem));
        }
        TEST_ASSERT_EQUAL_size_t(tda_al_pool_stats(pool).block_count, n);

        tda_al_pool_drop(pool);
    }
}

static void test_from_buf_rejects_a_buffer_without_room_for_a_block() {
    TEST_ASSERT_NULL(tda_al_pool_from_buf(mem, 8, block(16)));
    TEST_ASSERT_NULL(tda_al_pool_from_buf(mem, sizeof(mem), block(sizeof(mem))));
}

// drop gives nothing back to anyone, so the same buffer builds the next pool
static void test_from_buf_can_be_built_again_after_drop() {
    tda_Al *pool = tda_al_pool_from_buf(mem, sizeof(mem), block(32));
    TEST_ASSERT_NOT_NULL(tda_alloc(pool, block(32)));
    tda_al_pool_drop(pool);

    pool = tda_al_pool_from_buf(mem, sizeof(mem), block(32));
    TEST_ASSERT_NOT_NULL(pool);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_pool_stats(pool).used);

    tda_al_pool_drop(pool);
}

/* ========== composition ========== */

// a pool is an ordinary allocator, so an arena can back it
static void test_pool_can_live_in_another_allocator() {
    tda_Al parent = probe_al();

    tda_Al *pool = tda_al_pool_new(&parent, block(32), 2);
    TEST_ASSERT_NOT_NULL(pool);

    unsigned char *p = tda_alloc(pool, block(32));
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x3C, 32);
    TEST_ASSERT_EQUAL_UINT8(0x3C, p[31]);

    tda_al_pool_drop(pool);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_starts_with_every_block_free);
    RUN_TEST(test_new_raises_a_tiny_block_size);
    RUN_TEST(test_new_rounds_the_block_size_up);
    RUN_TEST(test_new_takes_the_alignment_from_the_layout);
    RUN_TEST(test_new_of_a_small_type_takes_a_pointer_per_block);
    RUN_TEST(test_drop_returns_everything_to_the_parent);
    RUN_TEST(test_new_cleans_up_after_a_failing_parent);
    RUN_TEST(test_drop_null_is_noop);

    RUN_TEST(test_alloc_takes_one_block);
    RUN_TEST(test_alloc_of_a_partial_block_still_costs_one);
    RUN_TEST(test_alloc_larger_than_a_block_fails);
    RUN_TEST(test_alloc_zero_is_null_and_costs_nothing);
    RUN_TEST(test_exhaustion_returns_null);
    RUN_TEST(test_every_block_is_aligned);
    RUN_TEST(test_blocks_do_not_overlap);

    RUN_TEST(test_dealloc_returns_the_block);
    RUN_TEST(test_freed_block_is_reused_first);
    RUN_TEST(test_block_taken_back_from_the_head_frees_again);
    RUN_TEST(test_exhausted_pool_recovers_after_a_free);
    RUN_TEST(test_dealloc_null_is_noop);

    RUN_TEST(test_alloc_zeroed_zeroes_the_block);
    RUN_TEST(test_alloc_zeroed_zeroes_a_recycled_block);
    RUN_TEST(test_alloc_zeroed_larger_than_a_block_fails);
    RUN_TEST(test_alloc_zeroed_rejects_overflow);

    RUN_TEST(test_realloc_within_a_block_stays_in_place);
    RUN_TEST(test_realloc_in_place_needs_no_free_block);
    RUN_TEST(test_realloc_beyond_a_block_fails_and_keeps_the_block);
    RUN_TEST(test_realloc_of_null_is_an_alloc);

    RUN_TEST(test_reset_frees_every_block);
    RUN_TEST(test_reset_recovers_from_a_scrambled_free_list);

    RUN_TEST(test_from_buf_fits_as_many_blocks_as_the_rest_holds);
    RUN_TEST(test_from_buf_wastes_less_than_a_block_at_the_end);
    RUN_TEST(test_from_buf_aligns_whatever_the_buffer);
    RUN_TEST(test_from_buf_aligns_to_the_blocks_own_alignment);
    RUN_TEST(test_from_buf_rejects_a_buffer_without_room_for_a_block);
    RUN_TEST(test_from_buf_can_be_built_again_after_drop);

    RUN_TEST(test_pool_can_live_in_another_allocator);

    return UNITY_END();
}
