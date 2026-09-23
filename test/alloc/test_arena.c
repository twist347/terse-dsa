#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"

#include <unity.h>

#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// the arena rounds every request up to this, so sizes and offsets are predictable
static constexpr size_t ALIGNMENT = alignof(max_align_t);

static size_t aligned(size_t size) {
    return (size + ALIGNMENT - 1) / ALIGNMENT * ALIGNMENT;
}

/* ========== probe allocator ==========
 *
 * Counts live blocks and can fail on demand, so the arena's own construction
 * and teardown paths can be checked for leaks.
 */

typedef struct {
    size_t live;
    size_t alloc_calls;
    size_t fail_after;
} Probe;

static Probe probe;

static void *probe_alloc(void *ctx, size_t size) {
    Probe *p = ctx;
    ++p->alloc_calls;

    if (p->alloc_calls > p->fail_after) {
        return nullptr;
    }

    void *ptr = malloc(size);
    if (ptr) {
        ++p->live;
    }
    return ptr;
}

static void probe_dealloc(void *ctx, void *ptr, size_t size) {
    Probe *p = ctx;
    (void) size;

    --p->live;
    free(ptr);
}

static tda_Al probe_al() {
    return (tda_Al){
        .ctx = &probe,
        .alloc = probe_alloc,
        .calloc = nullptr,
        .realloc = nullptr,
        .dealloc = probe_dealloc,
    };
}

void setUp() {
    probe = (Probe){.fail_after = SIZE_MAX};
}

void tearDown() {
}

/* ========== lifetime ========== */

static void test_new_starts_empty() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);
    TEST_ASSERT_NOT_NULL(arena);

    const tda_AlArenaStats st = tda_al_arena_stats(arena);
    TEST_ASSERT_EQUAL_size_t(1024, st.cap);
    TEST_ASSERT_EQUAL_size_t(0, st.used);
    TEST_ASSERT_EQUAL_size_t(1024, st.available);

    tda_al_arena_drop(arena);
}

// the arena borrows its parent: everything it took must go back on drop
static void test_drop_returns_everything_to_the_parent() {
    tda_Al parent = probe_al();

    tda_Al *arena = tda_al_arena_new(&parent, 256);
    TEST_ASSERT_NOT_NULL(arena);
    TEST_ASSERT_EQUAL_size_t(3, probe.live); // buffer, context, the tda_Al itself

    tda_al_arena_drop(arena);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// each construction step can fail; none of them may leak the earlier ones
static void test_new_cleans_up_after_a_failing_parent() {
    for (size_t fail_at = 0; fail_at < 3; ++fail_at) {
        probe = (Probe){.fail_after = fail_at};
        tda_Al parent = probe_al();

        TEST_ASSERT_NULL(tda_al_arena_new(&parent, 256));
        TEST_ASSERT_EQUAL_size_t(0, probe.live);
    }
}

static void test_drop_null_is_noop() {
    tda_al_arena_drop(nullptr);
}

/* ========== alloc ========== */

static void test_alloc_advances_by_the_aligned_size() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);

    void *p = tda_alloc(arena, 1);
    TEST_ASSERT_NOT_NULL(p);

    const tda_AlArenaStats st = tda_al_arena_stats(arena);
    TEST_ASSERT_EQUAL_size_t(aligned(1), st.used);
    TEST_ASSERT_EQUAL_size_t(1024 - aligned(1), st.available);

    tda_al_arena_drop(arena);
}

static void test_every_block_is_aligned() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);

    for (size_t size = 1; size <= 40; size += 7) {
        void *p = tda_alloc(arena, size);
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) p % ALIGNMENT);
    }

    tda_al_arena_drop(arena);
}

// consecutive blocks are disjoint — writing one must not disturb its neighbour
static void test_blocks_do_not_overlap() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);

    unsigned char *a = tda_alloc(arena, 16);
    unsigned char *b = tda_alloc(arena, 16);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);

    memset(a, 0x11, 16);
    memset(b, 0x22, 16);
    TEST_ASSERT_EQUAL_UINT8(0x11, a[15]);
    TEST_ASSERT_EQUAL_UINT8(0x22, b[0]);

    tda_al_arena_drop(arena);
}

static void test_alloc_zero_is_null_and_does_not_move_the_offset() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);

    TEST_ASSERT_NULL(tda_alloc(arena, 0));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

// exhaustion is a runtime state, not a bug: it returns nullptr and changes nothing
static void test_alloc_beyond_the_capacity_fails() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 64);

    TEST_ASSERT_NULL(tda_alloc(arena, 65));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_arena_stats(arena).used);

    // a request that still fits is unaffected by the failed one
    TEST_ASSERT_NOT_NULL(tda_alloc(arena, 16));

    tda_al_arena_drop(arena);
}

static void test_arena_can_be_filled_exactly() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4 * ALIGNMENT);

    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_NOT_NULL(tda_alloc(arena, ALIGNMENT));
    }

    const tda_AlArenaStats st = tda_al_arena_stats(arena);
    TEST_ASSERT_EQUAL_size_t(4 * ALIGNMENT, st.used);
    TEST_ASSERT_EQUAL_size_t(0, st.available);

    // and the next one has nowhere to go
    TEST_ASSERT_NULL(tda_alloc(arena, 1));

    tda_al_arena_drop(arena);
}

// rounding up must be accounted for: a small request still consumes a whole slot
static void test_capacity_accounts_for_rounding() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), ALIGNMENT);

    TEST_ASSERT_NOT_NULL(tda_alloc(arena, 1));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_arena_stats(arena).available);
    TEST_ASSERT_NULL(tda_alloc(arena, 1));

    tda_al_arena_drop(arena);
}

/* ========== calloc ========== */

static void test_calloc_zeroes_the_block() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);

    const unsigned char *p = tda_calloc(arena, 8, 4);
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_al_arena_drop(arena);
}

// a reused arena must not hand out the previous tenant's bytes through calloc
static void test_calloc_zeroes_reused_memory() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);

    unsigned char *first = tda_alloc(arena, 32);
    TEST_ASSERT_NOT_NULL(first);
    memset(first, 0xFF, 32);

    tda_al_arena_reset(arena);

    const unsigned char *second = tda_calloc(arena, 8, 4);
    TEST_ASSERT_EQUAL_PTR(first, second);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, second[i]);
    }

    tda_al_arena_drop(arena);
}

static void test_calloc_rejects_overflow() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);

    TEST_ASSERT_NULL(tda_calloc(arena, SIZE_MAX, 2));
    TEST_ASSERT_EQUAL_size_t(0, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

/* ========== realloc ========== */

// nothing lies past the last block, so it grows where it stands and costs only the growth
static void test_realloc_grows_the_last_block_in_place() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);

    unsigned char *p = tda_alloc(arena, 16);
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 16; ++i) {
        p[i] = (unsigned char) (i + 1);
    }

    unsigned char *q = tda_realloc(arena, p, 16, 64);
    TEST_ASSERT_EQUAL_PTR(p, q);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), q[i]);
    }
    TEST_ASSERT_EQUAL_size_t(64, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

// and shrinks the same way, handing the tail back to the arena
static void test_realloc_shrinks_the_last_block_in_place() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);

    void *p = tda_alloc(arena, 64);
    TEST_ASSERT_NOT_NULL(p);

    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(arena, p, 64, 20));
    TEST_ASSERT_EQUAL_size_t(aligned(20), tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

// a block with another one after it has nowhere to grow: it moves, and the old slot stays
// charged like every block the arena lends
static void test_realloc_moves_a_block_that_is_not_the_last() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 1024);

    unsigned char *p = tda_alloc(arena, 16);
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 16; ++i) {
        p[i] = (unsigned char) (i + 1);
    }
    TEST_ASSERT_NOT_NULL(tda_alloc(arena, 16));

    unsigned char *q = tda_realloc(arena, p, 16, 64);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_TRUE(p != q);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), q[i]);
    }
    TEST_ASSERT_EQUAL_size_t(16 + 16 + 64, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

// past the last block lies only the free tail: when growing there does not fit, nothing
// would, and the block is left as it was
static void test_realloc_of_the_last_block_beyond_the_capacity_fails() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 64);

    unsigned char *p = tda_alloc(arena, 32);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x5A, 32);

    TEST_ASSERT_NULL(tda_realloc(arena, p, 32, 65));
    TEST_ASSERT_EQUAL_size_t(32, tda_al_arena_stats(arena).used);
    TEST_ASSERT_EQUAL_UINT8(0x5A, p[31]);

    tda_al_arena_drop(arena);
}

static void test_realloc_of_null_is_an_alloc() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);

    TEST_ASSERT_NOT_NULL(tda_realloc(arena, nullptr, 0, 32));
    TEST_ASSERT_EQUAL_size_t(32, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

/* ========== dealloc / reset ========== */

// individual frees are deliberately no-ops: only reset reclaims space
static void test_dealloc_does_not_reclaim() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);

    void *p = tda_alloc(arena, 32);
    const size_t used = tda_al_arena_stats(arena).used;

    tda_dealloc(arena, p, 32);
    TEST_ASSERT_EQUAL_size_t(used, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

static void test_reset_reclaims_everything() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 256);

    void *first = tda_alloc(arena, 32);
    TEST_ASSERT_NOT_NULL(tda_alloc(arena, 32));

    tda_al_arena_reset(arena);

    const tda_AlArenaStats st = tda_al_arena_stats(arena);
    TEST_ASSERT_EQUAL_size_t(0, st.used);
    TEST_ASSERT_EQUAL_size_t(256, st.available);

    // the arena hands out the same memory again
    TEST_ASSERT_EQUAL_PTR(first, tda_alloc(arena, 32));

    tda_al_arena_drop(arena);
}

static void test_reset_of_an_untouched_arena_is_harmless() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 128);

    tda_al_arena_reset(arena);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

/* ========== from_buf ========== */

// the memory the tests build in; its size is well above any header
static alignas(max_align_t) unsigned char mem[512];

static bool inside(const void *ptr, size_t size, const void *buf, size_t buf_size) {
    const uintptr_t addr = (uintptr_t) ptr;
    const uintptr_t begin = (uintptr_t) buf;
    return addr >= begin && addr + size <= begin + buf_size;
}

static void test_from_buf_hands_out_the_buffer_itself() {
    tda_Al *arena = tda_al_arena_from_buf(mem, sizeof mem);
    TEST_ASSERT_NOT_NULL(arena);
    TEST_ASSERT_TRUE(inside(arena, sizeof(tda_Al), mem, sizeof mem));

    // the header is paid for out of the buffer, and the rest is the block
    const tda_AlArenaStats st = tda_al_arena_stats(arena);
    TEST_ASSERT_TRUE(st.cap > 0 && st.cap < sizeof mem);
    TEST_ASSERT_EQUAL_size_t(0, st.used);

    void *p = tda_alloc(arena, 16);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_TRUE(inside(p, 16, mem, sizeof mem));

    tda_al_arena_drop(arena);
}

// the caller's array may sit at any address; every block still has to be aligned
static void test_from_buf_aligns_whatever_the_buffer() {
    for (size_t off = 0; off < ALIGNMENT; ++off) {
        tda_Al *arena = tda_al_arena_from_buf(mem + off, sizeof mem - off);
        TEST_ASSERT_NOT_NULL(arena);

        for (size_t size = 1; size <= 40; size += 13) {
            void *p = tda_alloc(arena, size);
            TEST_ASSERT_NOT_NULL(p);
            TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) p % ALIGNMENT);
            TEST_ASSERT_TRUE(inside(p, size, mem + off, sizeof mem - off));
        }

        tda_al_arena_drop(arena);
    }
}

static void test_from_buf_rejects_a_buffer_too_small_for_the_header() {
    TEST_ASSERT_NULL(tda_al_arena_from_buf(mem, 8));
}

// every byte it hands out is inside the buffer, up to the last one
static void test_from_buf_runs_out_inside_the_buffer() {
    tda_Al *arena = tda_al_arena_from_buf(mem, sizeof mem);
    const size_t cap = tda_al_arena_stats(arena).cap;

    size_t taken = 0;
    for (void *p; (p = tda_alloc(arena, ALIGNMENT)); taken += ALIGNMENT) {
        TEST_ASSERT_TRUE(inside(p, ALIGNMENT, mem, sizeof mem));
    }
    TEST_ASSERT_EQUAL_size_t(cap / ALIGNMENT * ALIGNMENT, taken);

    tda_al_arena_drop(arena);
}

// drop gives nothing back to anyone, so the same buffer builds the next arena
static void test_from_buf_can_be_built_again_after_drop() {
    tda_Al *arena = tda_al_arena_from_buf(mem, sizeof mem);
    TEST_ASSERT_NOT_NULL(tda_alloc(arena, 64));
    tda_al_arena_drop(arena);

    arena = tda_al_arena_from_buf(mem, sizeof mem);
    TEST_ASSERT_NOT_NULL(arena);
    TEST_ASSERT_EQUAL_size_t(0, tda_al_arena_stats(arena).used);

    tda_al_arena_drop(arena);
}

/* ========== composition ========== */

// an arena is an ordinary allocator, so it can serve as another arena's parent
static void test_arena_can_feed_another_arena() {
    tda_Al *outer = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(outer);

    tda_Al *inner = tda_al_arena_new(outer, 256);
    TEST_ASSERT_NOT_NULL(inner);

    unsigned char *p = tda_alloc(inner, 64);
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x7E, 64);
    TEST_ASSERT_EQUAL_UINT8(0x7E, p[63]);

    TEST_ASSERT_EQUAL_size_t(256, tda_al_arena_stats(inner).cap);

    tda_al_arena_drop(inner);
    tda_al_arena_drop(outer);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_starts_empty);
    RUN_TEST(test_drop_returns_everything_to_the_parent);
    RUN_TEST(test_new_cleans_up_after_a_failing_parent);
    RUN_TEST(test_drop_null_is_noop);

    RUN_TEST(test_alloc_advances_by_the_aligned_size);
    RUN_TEST(test_every_block_is_aligned);
    RUN_TEST(test_blocks_do_not_overlap);
    RUN_TEST(test_alloc_zero_is_null_and_does_not_move_the_offset);
    RUN_TEST(test_alloc_beyond_the_capacity_fails);
    RUN_TEST(test_arena_can_be_filled_exactly);
    RUN_TEST(test_capacity_accounts_for_rounding);

    RUN_TEST(test_calloc_zeroes_the_block);
    RUN_TEST(test_calloc_zeroes_reused_memory);
    RUN_TEST(test_calloc_rejects_overflow);

    RUN_TEST(test_realloc_grows_the_last_block_in_place);
    RUN_TEST(test_realloc_shrinks_the_last_block_in_place);
    RUN_TEST(test_realloc_moves_a_block_that_is_not_the_last);
    RUN_TEST(test_realloc_of_the_last_block_beyond_the_capacity_fails);
    RUN_TEST(test_realloc_of_null_is_an_alloc);

    RUN_TEST(test_dealloc_does_not_reclaim);
    RUN_TEST(test_reset_reclaims_everything);
    RUN_TEST(test_reset_of_an_untouched_arena_is_harmless);

    RUN_TEST(test_from_buf_hands_out_the_buffer_itself);
    RUN_TEST(test_from_buf_aligns_whatever_the_buffer);
    RUN_TEST(test_from_buf_rejects_a_buffer_too_small_for_the_header);
    RUN_TEST(test_from_buf_runs_out_inside_the_buffer);
    RUN_TEST(test_from_buf_can_be_built_again_after_drop);

    RUN_TEST(test_arena_can_feed_another_arena);

    return UNITY_END();
}
