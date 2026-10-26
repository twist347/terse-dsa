#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/alloc/pool.h"

#include "support/layout.h"
#include "support/probe.h"
#include "support/status.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// malloc's own alignment, the one most tests below ask for when they ask for one
static constexpr size_t ALIGNMENT = alignof(max_align_t);

// at alignment 1 a block takes exactly its size, so offsets are plain sums
static tda_Layout bytes(size_t size) {
    return tda_test_layout(size, 1);
}

/* ========== probe allocator ==========
 *
 * The shared probe, bare: counts live blocks and can fail on demand, so the arena's
 * construction and teardown paths can be checked for leaks.
 */

static tda_TestProbe probe;

static tda_Al bare_al() {
    return tda_test_probe_bare(&probe);
}

void setUp() {
    tda_test_probe_reset(&probe);
}

void tearDown() {
}

/* ========== lifetime ========== */

static void test_init_starts_empty() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    const tda_ArenaStats st = tda_arena_stats(&arena);
    TEST_ASSERT_EQUAL_size_t(1024, st.cap);
    TEST_ASSERT_EQUAL_size_t(0, st.used);
    TEST_ASSERT_EQUAL_size_t(1024, st.free);

    tda_arena_deinit(&arena);
}

// the arena borrows its parent for its block alone, and gives it back on deinit
static void test_deinit_returns_the_block_to_the_parent() {
    tda_Al parent = bare_al();

    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, parent, &arena));
    TEST_ASSERT_EQUAL_size_t(1, probe.live); // the block

    tda_arena_deinit(&arena);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a refused block leaves nothing behind and the arena untouched
static void test_init_reports_a_failing_parent() {
    tda_test_probe_fail_after_next(&probe, 0);
    tda_Al parent = bare_al();

    tda_Arena arena;
    TDA_TEST_POISON(arena);
    TDA_TEST_STATUS(TDA_STATUS_ERR_NO_MEM, tda_arena_init_cap(256, parent, &arena));
    TDA_TEST_ASSERT_POISONED(arena);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the arena aligns each block itself, so its whole block asks the parent for nothing:
// a pool built for small alignments can be the parent
static void test_init_runs_on_a_parent_with_small_blocks_alignment() {
    tda_Pool pool;
    TDA_TEST_OK(tda_pool_init_cap(4, (tda_Layout){.size = 256, .align = 8}, tda_al_default(), &pool));

    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(128, tda_pool_to_al(&pool), &arena));
    const void *p = tda_alloc(tda_arena_to_al(&arena), tda_test_layout(8, 8));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % 8);

    tda_arena_deinit(&arena);
    tda_pool_deinit(&pool);
}

static void test_deinit_of_an_inert_arena_is_noop() {
    tda_Arena arena = {};
    tda_arena_deinit(&arena);
    tda_arena_deinit(&arena);
}

/* ========== view ========== */

// nothing in the arena points at itself, so it moves as any value does until a view
// is out; the view taken after the move works and the block still goes back
static void test_arena_moves_before_a_view_is_out() {
    tda_Al parent = bare_al();

    tda_Arena built;
    TDA_TEST_OK(tda_arena_init_cap(256, parent, &built));
    tda_Arena moved = built;

    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&moved), bytes(64)));
    TEST_ASSERT_EQUAL_size_t(64, tda_arena_stats(&moved).used);

    tda_arena_deinit(&moved);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a container compares the views it was given to decide whether blocks can change hands
static void test_views_of_one_arena_are_one_allocator() {
    unsigned char buf_a[64], buf_b[64];
    tda_Arena a = tda_arena_init_buf(buf_a, sizeof(buf_a));
    tda_Arena b = tda_arena_init_buf(buf_b, sizeof(buf_b));

    TEST_ASSERT_TRUE(tda_al_eq(tda_arena_to_al(&a), tda_arena_to_al(&a)));
    TEST_ASSERT_FALSE(tda_al_eq(tda_arena_to_al(&a), tda_arena_to_al(&b)));
    TEST_ASSERT_FALSE(tda_al_eq(tda_arena_to_al(&a), tda_al_default()));

    tda_arena_deinit(&b);
    tda_arena_deinit(&a);
}

/* ========== alloc ========== */

// nothing is rounded up: a block takes its size and no more
static void test_alloc_advances_by_the_size() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    const void *p = tda_alloc(tda_arena_to_al(&arena), bytes(1));
    TEST_ASSERT_NOT_NULL(p);

    const tda_ArenaStats st = tda_arena_stats(&arena);
    TEST_ASSERT_EQUAL_size_t(1, st.used);
    TEST_ASSERT_EQUAL_size_t(1024 - 1, st.free);

    tda_arena_deinit(&arena);
}

// whatever came before, a block starts on the alignment asked for, up to a page
static void test_every_block_has_the_alignment_asked_for() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(32 * 1024, tda_al_default(), &arena));

    for (size_t align = 1; align <= 4096; align *= 2) {
        TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(3))); // knocks the offset off any grid
        const void *p = tda_alloc(tda_arena_to_al(&arena), tda_test_layout(5, align));
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % align);
    }

    tda_arena_deinit(&arena);
}

// small blocks sit back to back; one with a larger alignment skips to its boundary, and
// the padding is all it costs
static void test_alloc_pads_to_the_alignment_and_no_further() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    const unsigned char *c1 = tda_alloc(tda_arena_to_al(&arena), bytes(1));
    const unsigned char *c2 = tda_alloc(tda_arena_to_al(&arena), bytes(1));
    int32_t *i = TDA_ALLOC(int32_t, tda_arena_to_al(&arena), 1);
    TEST_ASSERT_EQUAL_PTR(c1 + 1, c2);
    TEST_ASSERT_EQUAL_PTR(c1 + 4, (unsigned char *) i); // from offset 2 to 4
    TEST_ASSERT_EQUAL_size_t(8, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// consecutive blocks are disjoint: writing one must not disturb its neighbour
static void test_blocks_do_not_overlap() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    unsigned char *a = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    unsigned char *b = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);

    memset(a, 0x11, 16);
    memset(b, 0x22, 16);
    TEST_ASSERT_EQUAL_UINT8(0x11, a[15]);
    TEST_ASSERT_EQUAL_UINT8(0x22, b[0]);

    tda_arena_deinit(&arena);
}

static void test_alloc_zero_is_null_and_does_not_move_the_offset() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    TEST_ASSERT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(0)));
    TEST_ASSERT_EQUAL_size_t(0, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// exhaustion is a runtime state, not a bug: it returns nullptr and changes nothing
static void test_alloc_beyond_the_capacity_fails() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(64, tda_al_default(), &arena));

    TEST_ASSERT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(65)));
    TEST_ASSERT_EQUAL_size_t(0, tda_arena_stats(&arena).used);

    // a request that still fits is unaffected by the failed one
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(16)));

    tda_arena_deinit(&arena);
}

static void test_arena_can_be_filled_exactly() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4 * ALIGNMENT, tda_al_default(), &arena));

    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(ALIGNMENT)));
    }

    const tda_ArenaStats st = tda_arena_stats(&arena);
    TEST_ASSERT_EQUAL_size_t(4 * ALIGNMENT, st.used);
    TEST_ASSERT_EQUAL_size_t(0, st.free);

    // and the next one has nowhere to go
    TEST_ASSERT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(1)));

    tda_arena_deinit(&arena);
}

// any cap is taken as it is, and all of it can be handed out
static void test_cap_is_taken_as_given() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(100, tda_al_default(), &arena));

    const tda_ArenaStats st = tda_arena_stats(&arena);
    TEST_ASSERT_EQUAL_size_t(100, st.cap);
    TEST_ASSERT_EQUAL_size_t(100, st.free);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(100)));

    tda_arena_deinit(&arena);
}

// the padding before an aligned block is charged to the capacity like the block
static void test_capacity_accounts_for_padding() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(2 * ALIGNMENT, tda_al_default(), &arena));

    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(1)));
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), tda_test_layout(ALIGNMENT, ALIGNMENT)));
    TEST_ASSERT_EQUAL_size_t(0, tda_arena_stats(&arena).free);
    TEST_ASSERT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(1)));

    tda_arena_deinit(&arena);
}

/* ========== alloc_zeroed ========== */

static void test_alloc_zeroed_zeroes_the_block() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    const unsigned char *p = tda_alloc_zeroed(tda_arena_to_al(&arena), bytes(32));
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_arena_deinit(&arena);
}

// a reused arena must not hand out the previous tenant's bytes through alloc_zeroed
static void test_alloc_zeroed_zeroes_reused_memory() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    unsigned char *first = tda_alloc(tda_arena_to_al(&arena), bytes(32));
    TEST_ASSERT_NOT_NULL(first);
    memset(first, 0xFF, 32);

    tda_arena_reset(&arena);

    const unsigned char *second = tda_alloc_zeroed(tda_arena_to_al(&arena), bytes(32));
    TEST_ASSERT_EQUAL_PTR(first, second);
    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, second[i]);
    }

    tda_arena_deinit(&arena);
}

static void test_alloc_zeroed_rejects_overflow() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    TEST_ASSERT_NULL(TDA_ALLOC_ZEROED(int32_t, tda_arena_to_al(&arena), SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(0, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

/* ========== realloc ========== */

// nothing lies past the last block, so it grows in place and costs only the growth
static void test_realloc_grows_the_last_block_in_place() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    unsigned char *p = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 16; ++i) {
        p[i] = (unsigned char) (i + 1);
    }

    const unsigned char *q = tda_realloc(tda_arena_to_al(&arena), p, bytes(16), 64);
    TEST_ASSERT_EQUAL_PTR(p, q);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), q[i]);
    }
    TEST_ASSERT_EQUAL_size_t(64, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// and shrinks the same way, handing the tail back to the arena
static void test_realloc_shrinks_the_last_block_in_place() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    void *p = tda_alloc(tda_arena_to_al(&arena), bytes(64));
    TEST_ASSERT_NOT_NULL(p);

    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(tda_arena_to_al(&arena), p, bytes(64), 20));
    TEST_ASSERT_EQUAL_size_t(20, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// a block with another one after it has to move to grow, and its old slot stays charged
// like any block the arena lends
static void test_realloc_moves_a_block_that_is_not_the_last() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    unsigned char *p = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_NOT_NULL(p);
    for (size_t i = 0; i < 16; ++i) {
        p[i] = (unsigned char) (i + 1);
    }
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(16)));

    const unsigned char *q = tda_realloc(tda_arena_to_al(&arena), p, bytes(16), 64);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_TRUE(p != q);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), q[i]);
    }
    TEST_ASSERT_EQUAL_size_t(16 + 16 + 64, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// shrinking a block that is not the last needs no new room: it stays in place, so even a
// full arena can do it
static void test_realloc_shrinks_a_block_that_is_not_the_last_in_place() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(4 * ALIGNMENT, tda_al_default(), &arena));

    void *p = tda_alloc(tda_arena_to_al(&arena), bytes(2 * ALIGNMENT));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(2 * ALIGNMENT)));
    TEST_ASSERT_EQUAL_size_t(0, tda_arena_stats(&arena).free);

    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(tda_arena_to_al(&arena), p, bytes(2 * ALIGNMENT), 1));
    TEST_ASSERT_EQUAL_size_t(4 * ALIGNMENT, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// past the last block lies only the free tail: if growing there does not fit, nothing
// would, and the block is left as it was
static void test_realloc_of_the_last_block_beyond_the_capacity_fails() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(64, tda_al_default(), &arena));

    unsigned char *p = tda_alloc(tda_arena_to_al(&arena), bytes(32));
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x5A, 32);

    TEST_ASSERT_NULL(tda_realloc(tda_arena_to_al(&arena), p, bytes(32), 65));
    TEST_ASSERT_EQUAL_size_t(32, tda_arena_stats(&arena).used);
    TEST_ASSERT_EQUAL_UINT8(0x5A, p[31]);

    tda_arena_deinit(&arena);
}

static void test_realloc_from_null_is_an_alloc() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    TEST_ASSERT_NOT_NULL(tda_realloc(tda_arena_to_al(&arena), nullptr, bytes(0), 32));
    TEST_ASSERT_EQUAL_size_t(32, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

/* ========== dealloc / reset ========== */

// individual frees are deliberately no-ops: only reset reclaims space
static void test_dealloc_does_not_reclaim() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    void *p = tda_alloc(tda_arena_to_al(&arena), bytes(32));
    const size_t used = tda_arena_stats(&arena).used;

    tda_dealloc(tda_arena_to_al(&arena), p, bytes(32));
    TEST_ASSERT_EQUAL_size_t(used, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

static void test_reset_reclaims_everything() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &arena));

    const void *first = tda_alloc(tda_arena_to_al(&arena), bytes(32));
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(32)));

    tda_arena_reset(&arena);

    const tda_ArenaStats st = tda_arena_stats(&arena);
    TEST_ASSERT_EQUAL_size_t(0, st.used);
    TEST_ASSERT_EQUAL_size_t(256, st.free);

    // the arena hands out the same memory again
    TEST_ASSERT_EQUAL_PTR(first, tda_alloc(tda_arena_to_al(&arena), bytes(32)));

    tda_arena_deinit(&arena);
}

static void test_reset_of_an_untouched_arena_is_harmless() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(128, tda_al_default(), &arena));

    tda_arena_reset(&arena);
    TEST_ASSERT_EQUAL_size_t(0, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

/* ========== mark / rewind ========== */

static void test_rewind_takes_back_what_came_after_the_mark() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    const void *kept = tda_alloc(tda_arena_to_al(&arena), bytes(40));
    TEST_ASSERT_NOT_NULL(kept);
    const tda_ArenaMark mark = tda_arena_mark(&arena);
    TEST_ASSERT_EQUAL_size_t(40, mark.offset_);

    const void *scratch = tda_alloc(tda_arena_to_al(&arena), bytes(100));
    TEST_ASSERT_NOT_NULL(scratch);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(7)));

    tda_arena_rewind(&arena, mark);
    TEST_ASSERT_EQUAL_size_t(40, tda_arena_stats(&arena).used);

    // the room is handed out again from where the mark stood
    TEST_ASSERT_EQUAL_PTR(scratch, tda_alloc(tda_arena_to_al(&arena), bytes(8)));

    tda_arena_deinit(&arena);
}

static void test_a_rewind_with_nothing_since_changes_nothing() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(16)));

    tda_arena_rewind(&arena, tda_arena_mark(&arena));

    TEST_ASSERT_EQUAL_size_t(16, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// marks nest: each rewind goes back to its own mark, innermost first
static void test_marks_nest() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    const tda_ArenaMark outer = tda_arena_mark(&arena);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(16)));
    const tda_ArenaMark inner = tda_arena_mark(&arena);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(32)));

    tda_arena_rewind(&arena, inner);
    TEST_ASSERT_EQUAL_size_t(16, tda_arena_stats(&arena).used);

    tda_arena_rewind(&arena, outer);
    TEST_ASSERT_EQUAL_size_t(0, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// the outer rewind takes the inner scratch with it, never rewound on its own
static void test_rewinding_an_outer_mark_skips_the_inner_ones() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    void *first = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_NOT_NULL(first);
    const tda_ArenaMark outer = tda_arena_mark(&arena);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(16)));
    (void) tda_arena_mark(&arena);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(16)));

    tda_arena_rewind(&arena, outer);
    TEST_ASSERT_EQUAL_size_t(16, tda_arena_stats(&arena).used);

    // and the floor is the one from before the outer mark, not the inner one's: the first
    // block, last again, grows in place
    TEST_ASSERT_EQUAL_PTR(first, tda_realloc(tda_arena_to_al(&arena), first, bytes(16), 64));

    tda_arena_deinit(&arena);
}

// why the floor exists: a block from before the mark, last when the mark was taken, must
// not grow in place past it, or the rewind hands its tail out a second time
static void test_a_block_from_under_the_mark_moves_to_grow() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    unsigned char *old = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_NOT_NULL(old);
    memset(old, 0xab, 16);

    const tda_ArenaMark mark = tda_arena_mark(&arena);

    const unsigned char *grown = tda_realloc(tda_arena_to_al(&arena), old, bytes(16), 64);
    TEST_ASSERT_NOT_NULL(grown);
    TEST_ASSERT_TRUE(grown != old);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0xab, grown[i]);
    }

    tda_arena_rewind(&arena, mark);

    // what the rewind frees is the moved copy's room, and 'old' is whole
    unsigned char *next = tda_alloc(tda_arena_to_al(&arena), bytes(64));
    TEST_ASSERT_NOT_NULL(next);
    memset(next, 0, 64);
    for (size_t i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0xab, old[i]);
    }

    tda_arena_deinit(&arena);
}

// and shrinking one leaves the offset where it is, so the mark stays below it
static void test_a_block_from_under_the_mark_shrinks_where_it_stands() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    void *old = tda_alloc(tda_arena_to_al(&arena), bytes(64));
    TEST_ASSERT_NOT_NULL(old);
    const tda_ArenaMark mark = tda_arena_mark(&arena);

    TEST_ASSERT_EQUAL_PTR(old, tda_realloc(tda_arena_to_al(&arena), old, bytes(64), 16));
    TEST_ASSERT_EQUAL_size_t(64, tda_arena_stats(&arena).used);

    tda_arena_rewind(&arena, mark);
    TEST_ASSERT_EQUAL_size_t(64, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// a block from above the mark grows in place as usual
static void test_a_block_from_above_the_mark_grows_in_place() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(16)));
    const tda_ArenaMark mark = tda_arena_mark(&arena);

    void *p = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(tda_arena_to_al(&arena), p, bytes(16), 64));
    TEST_ASSERT_EQUAL_size_t(16 + 64, tda_arena_stats(&arena).used);

    tda_arena_rewind(&arena, mark);
    TEST_ASSERT_EQUAL_size_t(16, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

// the rewind puts the floor back, so the block that had to move under the mark grows in
// place once the mark is gone
static void test_the_rewind_lowers_the_floor_again() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    void *p = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_NOT_NULL(p);

    tda_arena_rewind(&arena, tda_arena_mark(&arena));

    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(tda_arena_to_al(&arena), p, bytes(16), 64));

    tda_arena_deinit(&arena);
}

// a reset drops the floor as well as the blocks
static void test_reset_drops_the_floor() {
    tda_Arena arena;
    TDA_TEST_OK(tda_arena_init_cap(1024, tda_al_default(), &arena));

    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(16)));
    (void) tda_arena_mark(&arena);
    tda_arena_reset(&arena);

    void *p = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_PTR(p, tda_realloc(tda_arena_to_al(&arena), p, bytes(16), 64));

    tda_arena_deinit(&arena);
}

// marks work the same in a caller's buffer: they track the offset, not the parent
static void test_mark_and_rewind_in_a_buffer() {
    alignas(max_align_t) unsigned char buf[512];
    tda_Arena arena = tda_arena_init_buf(buf, sizeof(buf));

    const size_t before = tda_arena_stats(&arena).used;
    const tda_ArenaMark mark = tda_arena_mark(&arena);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(100)));

    tda_arena_rewind(&arena, mark);
    TEST_ASSERT_EQUAL_size_t(before, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

/* ========== init_buf ========== */

// the memory the tests build in
static alignas(max_align_t) unsigned char mem[512];

static bool inside(const void *ptr, size_t size, const void *buf, size_t buf_size) {
    const uintptr_t addr = (uintptr_t) ptr;
    const uintptr_t begin = (uintptr_t) buf;

    return addr >= begin && addr + size <= begin + buf_size;
}

// the arena holds its own fields, so the whole buffer is the block
static void test_init_buf_hands_out_the_whole_buffer() {
    tda_Arena arena = tda_arena_init_buf(mem, sizeof(mem));

    const tda_ArenaStats st = tda_arena_stats(&arena);
    TEST_ASSERT_EQUAL_size_t(sizeof(mem), st.cap);
    TEST_ASSERT_EQUAL_size_t(0, st.used);

    const void *p = tda_alloc(tda_arena_to_al(&arena), bytes(16));
    TEST_ASSERT_EQUAL_PTR(mem, p);

    tda_arena_deinit(&arena);
}

// the caller's array may sit at any address; every block still has to be aligned
static void test_init_buf_aligns_whatever_the_buffer() {
    for (size_t off = 0; off < ALIGNMENT; ++off) {
        tda_Arena arena = tda_arena_init_buf(mem + off, sizeof(mem) - off);

        for (size_t size = 1; size <= 40; size += 13) {
            const void *p = tda_alloc(tda_arena_to_al(&arena), tda_test_layout(size, ALIGNMENT));
            TEST_ASSERT_NOT_NULL(p);
            TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) p % ALIGNMENT);
            TEST_ASSERT_TRUE(inside(p, size, mem + off, sizeof(mem) - off));
        }

        tda_arena_deinit(&arena);
    }
}

// a buffer of one byte is an arena of one byte
static void test_init_buf_on_the_smallest_buffer_hands_out_a_byte() {
    tda_Arena arena = tda_arena_init_buf(mem, 1);

    TEST_ASSERT_EQUAL_size_t(1, tda_arena_stats(&arena).cap);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(1)));
    TEST_ASSERT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(1)));

    tda_arena_deinit(&arena);
}

// every byte it hands out is inside the buffer, up to the last one
static void test_init_buf_runs_out_inside_the_buffer() {
    tda_Arena arena = tda_arena_init_buf(mem, sizeof(mem));
    const size_t cap = tda_arena_stats(&arena).cap;

    size_t taken = 0;
    for (void *p; (p = tda_alloc(tda_arena_to_al(&arena), bytes(1))); ++taken) {
        TEST_ASSERT_TRUE(inside(p, 1, mem, sizeof(mem)));
    }
    TEST_ASSERT_EQUAL_size_t(cap, taken);

    tda_arena_deinit(&arena);
}

// the block runs up to the last byte of the buffer, whatever its size
static void test_init_buf_hands_out_the_buffer_to_its_last_byte() {
    for (size_t size = sizeof(mem) - ALIGNMENT; size < sizeof(mem); ++size) {
        tda_Arena arena = tda_arena_init_buf(mem, size);

        uintptr_t end = 0;
        for (void *p; (p = tda_alloc(tda_arena_to_al(&arena), bytes(1)));) {
            end = (uintptr_t) p + 1;
        }
        TEST_ASSERT_EQUAL_UINT64((uintptr_t) mem + size, end);

        tda_arena_deinit(&arena);
    }
}

// deinit gives nothing back to anyone, so the same buffer builds the next arena
static void test_init_buf_can_be_built_again_after_deinit() {
    tda_Arena arena = tda_arena_init_buf(mem, sizeof(mem));
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&arena), bytes(64)));
    tda_arena_deinit(&arena);

    arena = tda_arena_init_buf(mem, sizeof(mem));
    TEST_ASSERT_EQUAL_size_t(0, tda_arena_stats(&arena).used);

    tda_arena_deinit(&arena);
}

/* ========== composition ========== */

// an arena is an ordinary allocator, so it can serve as another arena's parent
static void test_arena_can_feed_another_arena() {
    tda_Arena outer;
    TDA_TEST_OK(tda_arena_init_cap(4096, tda_al_default(), &outer));

    tda_Arena inner;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_arena_to_al(&outer), &inner));

    unsigned char *p = tda_alloc(tda_arena_to_al(&inner), bytes(64));
    TEST_ASSERT_NOT_NULL(p);
    memset(p, 0x7E, 64);
    TEST_ASSERT_EQUAL_UINT8(0x7E, p[63]);

    TEST_ASSERT_EQUAL_size_t(256, tda_arena_stats(&inner).cap);

    tda_arena_deinit(&inner);
    tda_arena_deinit(&outer);
}

// deinit leaves the inert state, so a second one is safe
static void test_deinit_twice_is_noop() {
    tda_Arena x;
    TDA_TEST_OK(tda_arena_init_cap(256, tda_al_default(), &x));
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&x), (tda_Layout){.size = 8, .align = 8}));

    tda_arena_deinit(&x);
    TEST_ASSERT_NULL(x.data_);

    tda_arena_deinit(&x);
}


// a mark holds offsets only, so it outlives a move of its arena
static void test_a_mark_survives_a_move_of_its_arena() {
    tda_Arena built;
    TDA_TEST_OK(tda_arena_init_cap(256, bare_al(), &built));
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&built), bytes(16)));
    const tda_ArenaMark mark = tda_arena_mark(&built);
    TEST_ASSERT_NOT_NULL(tda_alloc(tda_arena_to_al(&built), bytes(64)));

    tda_Arena moved = built;
    tda_arena_rewind(&moved, mark);
    TEST_ASSERT_EQUAL_size_t(16, tda_arena_stats(&moved).used);

    tda_arena_deinit(&moved);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_init_starts_empty);
    RUN_TEST(test_deinit_returns_the_block_to_the_parent);
    RUN_TEST(test_init_reports_a_failing_parent);
    RUN_TEST(test_init_runs_on_a_parent_with_small_blocks_alignment);
    RUN_TEST(test_deinit_of_an_inert_arena_is_noop);

    RUN_TEST(test_arena_moves_before_a_view_is_out);
    RUN_TEST(test_views_of_one_arena_are_one_allocator);

    RUN_TEST(test_alloc_advances_by_the_size);
    RUN_TEST(test_every_block_has_the_alignment_asked_for);
    RUN_TEST(test_alloc_pads_to_the_alignment_and_no_further);
    RUN_TEST(test_blocks_do_not_overlap);
    RUN_TEST(test_alloc_zero_is_null_and_does_not_move_the_offset);
    RUN_TEST(test_alloc_beyond_the_capacity_fails);
    RUN_TEST(test_arena_can_be_filled_exactly);
    RUN_TEST(test_capacity_accounts_for_padding);
    RUN_TEST(test_cap_is_taken_as_given);

    RUN_TEST(test_alloc_zeroed_zeroes_the_block);
    RUN_TEST(test_alloc_zeroed_zeroes_reused_memory);
    RUN_TEST(test_alloc_zeroed_rejects_overflow);

    RUN_TEST(test_realloc_grows_the_last_block_in_place);
    RUN_TEST(test_realloc_shrinks_the_last_block_in_place);
    RUN_TEST(test_realloc_moves_a_block_that_is_not_the_last);
    RUN_TEST(test_realloc_shrinks_a_block_that_is_not_the_last_in_place);
    RUN_TEST(test_realloc_of_the_last_block_beyond_the_capacity_fails);
    RUN_TEST(test_realloc_from_null_is_an_alloc);

    RUN_TEST(test_dealloc_does_not_reclaim);
    RUN_TEST(test_reset_reclaims_everything);
    RUN_TEST(test_reset_of_an_untouched_arena_is_harmless);

    RUN_TEST(test_rewind_takes_back_what_came_after_the_mark);
    RUN_TEST(test_a_rewind_with_nothing_since_changes_nothing);
    RUN_TEST(test_marks_nest);
    RUN_TEST(test_rewinding_an_outer_mark_skips_the_inner_ones);
    RUN_TEST(test_a_block_from_under_the_mark_moves_to_grow);
    RUN_TEST(test_a_block_from_under_the_mark_shrinks_where_it_stands);
    RUN_TEST(test_a_block_from_above_the_mark_grows_in_place);
    RUN_TEST(test_the_rewind_lowers_the_floor_again);
    RUN_TEST(test_reset_drops_the_floor);
    RUN_TEST(test_mark_and_rewind_in_a_buffer);

    RUN_TEST(test_init_buf_hands_out_the_whole_buffer);
    RUN_TEST(test_init_buf_aligns_whatever_the_buffer);
    RUN_TEST(test_init_buf_on_the_smallest_buffer_hands_out_a_byte);
    RUN_TEST(test_init_buf_runs_out_inside_the_buffer);
    RUN_TEST(test_init_buf_hands_out_the_buffer_to_its_last_byte);
    RUN_TEST(test_init_buf_can_be_built_again_after_deinit);

    RUN_TEST(test_arena_can_feed_another_arena);

    RUN_TEST(test_deinit_twice_is_noop);

    RUN_TEST(test_a_mark_survives_a_move_of_its_arena);

    return UNITY_END();
}
