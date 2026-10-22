#include "tda/alloc/aligned.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/ds/hmap.h"
#include "tda/ds/list.h"

#include "support/probe.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ========== probe allocator ==========
 *
 * The shared probe (test/support/probe.h) in its "full" flavour: its own calloc and
 * realloc are there so the tests can watch that the aligned allocator never reaches
 * for them — either one would hand back a block aligned only for the parent.
 */

static tda_TestProbe probe;

static tda_Al probe_al() {
    return tda_test_probe_full(&probe);
}

// what a request for 'size' costs the parent
static size_t parent_size(size_t size, size_t alignment) {
    return size + alignment - 1 + sizeof(void *);
}

void setUp() {
    tda_test_probe_reset(&probe);
}

void tearDown() {
}

/* ========== lifetime ========== */

static void test_new_takes_two_blocks_and_drop_returns_them() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);
    TEST_ASSERT_EQUAL_size_t(2, probe.live);

    tda_al_aligned_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// the ctx is taken first, so a parent that refuses either request must leak nothing
static void test_new_cleans_up_after_a_failing_parent() {
    tda_Al parent = probe_al();

    tda_test_probe_fail_after_next(&probe, 0);
    TEST_ASSERT_NULL(tda_al_aligned_new(&parent, 64));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);

    tda_test_probe_reset(&probe);
    tda_test_probe_fail_after_next(&probe, 1);
    TEST_ASSERT_NULL(tda_al_aligned_new(&parent, 64));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_drop_null_is_noop() {
    tda_al_aligned_drop(nullptr);
}

/* ========== alloc ========== */

static void test_every_block_is_aligned() {
    static constexpr size_t alignments[] = {64, 128, 256};

    for (size_t a = 0; a < sizeof(alignments) / sizeof(alignments[0]); ++a) {
        tda_Al *al = tda_al_aligned_new(tda_al_default(), alignments[a]);
        TEST_ASSERT_NOT_NULL(al);

        // walk the sizes across a whole period, so every offset the parent can
        // return is exercised
        for (size_t size = 1; size <= 2 * alignments[a]; ++size) {
            unsigned char *ptr = tda_alloc(al, size);
            TEST_ASSERT_NOT_NULL(ptr);
            TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) ptr % alignments[a]);

            memset(ptr, 0xAB, size); // the whole span has to be ours
            tda_dealloc(al, ptr, size);
        }

        tda_al_aligned_drop(al);
    }
}

static void test_blocks_do_not_overlap() {
    tda_Al *al = tda_al_aligned_new(tda_al_default(), 64);
    TEST_ASSERT_NOT_NULL(al);

    unsigned char *a = tda_alloc(al, 100);
    unsigned char *b = tda_alloc(al, 100);
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);

    memset(a, 0x11, 100);
    memset(b, 0x22, 100);
    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x11, a[i]);
    }

    tda_dealloc(al, a, 100);
    tda_dealloc(al, b, 100);
    tda_al_aligned_drop(al);
}

// the parent is asked for the padding and the word before the block, and gets the
// same size back on free, so the sized dealloc stays correct
static void test_the_parent_sees_the_padded_size_both_ways() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);

    void *ptr = tda_alloc(al, 100);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(parent_size(100, 64), probe.last_alloc_size);

    tda_dealloc(al, ptr, 100);
    TEST_ASSERT_EQUAL_size_t(probe.last_alloc_size, probe.last_dealloc_size);

    tda_al_aligned_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_alloc_zero_is_null_and_asks_the_parent_nothing() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);

    const size_t before = tda_test_probe_requests(&probe);
    TEST_ASSERT_NULL(tda_alloc(al, 0));
    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));

    tda_al_aligned_drop(al);
}

static void test_alloc_rejects_a_size_the_padding_would_overflow() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);

    const size_t before = tda_test_probe_requests(&probe);
    TEST_ASSERT_NULL(tda_alloc(al, SIZE_MAX));
    TEST_ASSERT_NULL(tda_alloc(al, SIZE_MAX - sizeof(void *)));
    TEST_ASSERT_EQUAL_size_t(before, tda_test_probe_requests(&probe));

    tda_al_aligned_drop(al);
}

static void test_alloc_fails_when_the_parent_does() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);

    tda_test_probe_fail_after_next(&probe, 0);
    TEST_ASSERT_NULL(tda_alloc(al, 100));
    TEST_ASSERT_EQUAL_size_t(2, probe.live); // still just the allocator's own two

    tda_test_probe_reset(&probe);
    tda_al_aligned_drop(al);
}

/* ========== dealloc ========== */

static void test_dealloc_null_is_noop() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);

    const size_t before = probe.dealloc_calls;
    tda_dealloc(al, nullptr, 0);
    TEST_ASSERT_EQUAL_size_t(before, probe.dealloc_calls);

    tda_al_aligned_drop(al);
}

/* ========== calloc and realloc ========== */

// no calloc of its own, so tda_calloc must build one on this alloc, not on the
// parent's, which would align the block to the parent instead
static void test_calloc_zeroes_an_aligned_block_without_the_parents_calloc() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);

    unsigned char *ptr = tda_calloc(al, 8, 16);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) ptr % 64);
    TEST_ASSERT_EQUAL_size_t(0, probe.calloc_calls);
    for (size_t i = 0; i < 8 * 16; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, ptr[i]);
    }

    tda_dealloc(al, ptr, 8 * 16);
    tda_al_aligned_drop(al);
}

// same for realloc: the parent's would keep the bytes but not the alignment
static void test_realloc_keeps_the_bytes_and_the_alignment() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);

    unsigned char *ptr = tda_alloc(al, 100);
    TEST_ASSERT_NOT_NULL(ptr);
    memset(ptr, 0x5A, 100);

    unsigned char *grown = tda_realloc(al, ptr, 100, 500);
    TEST_ASSERT_NOT_NULL(grown);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) grown % 64);
    TEST_ASSERT_EQUAL_size_t(0, probe.realloc_calls);
    for (size_t i = 0; i < 100; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5A, grown[i]);
    }

    unsigned char *shrunk = tda_realloc(al, grown, 500, 50);
    TEST_ASSERT_NOT_NULL(shrunk);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) shrunk % 64);
    for (size_t i = 0; i < 50; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0x5A, shrunk[i]);
    }

    tda_dealloc(al, shrunk, 50);
    tda_al_aligned_drop(al);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_realloc_to_zero_gives_the_block_back() {
    tda_Al parent = probe_al();

    tda_Al *al = tda_al_aligned_new(&parent, 64);
    TEST_ASSERT_NOT_NULL(al);

    void *ptr = tda_alloc(al, 100);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(3, probe.live);

    TEST_ASSERT_NULL(tda_realloc(al, ptr, 100, 0));
    TEST_ASSERT_EQUAL_size_t(2, probe.live);

    tda_al_aligned_drop(al);
}

/* ========== nesting ========== */

// alignment is a property of the allocator, so it composes: the arena's own step is
// max_align_t and the wrapper lifts what it hands out to 128
static void test_over_an_arena() {
    tda_Al *arena = tda_al_arena_new(tda_al_default(), 4096);
    TEST_ASSERT_NOT_NULL(arena);

    tda_Al *al = tda_al_aligned_new(arena, 128);
    TEST_ASSERT_NOT_NULL(al);

    for (size_t i = 0; i < 4; ++i) {
        void *ptr = tda_alloc(al, 100);
        TEST_ASSERT_NOT_NULL(ptr);
        TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) ptr % 128);
    }

    tda_al_aligned_drop(al);
    tda_al_arena_drop(arena);
}

static void test_over_itself() {
    tda_Al *inner = tda_al_aligned_new(tda_al_default(), 64);
    TEST_ASSERT_NOT_NULL(inner);

    tda_Al *outer = tda_al_aligned_new(inner, 256);
    TEST_ASSERT_NOT_NULL(outer);

    void *ptr = tda_alloc(outer, 300);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) ptr % 256);

    tda_dealloc(outer, ptr, 300);
    tda_al_aligned_drop(outer);
    tda_al_aligned_drop(inner);
}

/* ========== over a parent that aligns too little ==========
 *
 * Every block of this parent sits a pointer's alignment past a max_align_t boundary,
 * below what the tda_Al contract asks wherever max_align_t is the wider of the two.
 * list and hmap would put their elems there unaligned; the aligned allocator in between
 * is the fix the contract names.
 */

// as narrow as an allocator can be and still hold the aligned allocator's own state,
// which keeps pointers in the parent's blocks
static constexpr size_t NARROW_SHIFT = alignof(void *);

static void *narrow_alloc(void *ctx, size_t size) {
    unsigned char *base = tda_alloc(ctx, size + alignof(max_align_t));
    return base ? base + NARROW_SHIFT : nullptr;
}

static void narrow_dealloc(void *ctx, void *ptr, size_t size) {
    if (ptr) {
        tda_dealloc(ctx, (unsigned char *) ptr - NARROW_SHIFT, size + alignof(max_align_t));
    }
}

static tda_Al narrow_al() {
    return (tda_Al){.ctx = tda_al_default(), .alloc = narrow_alloc, .dealloc = narrow_dealloc};
}

static void test_the_narrow_parent_misses_max_align_t() {
    // where max_align_t is no wider than a pointer (arm64 macOS), a pointer-aligned
    // block already meets the contract and there is no narrow parent to build
    if (alignof(max_align_t) <= NARROW_SHIFT) {
        TEST_IGNORE_MESSAGE("max_align_t is no wider than a pointer here");
    }

    tda_Al narrow = narrow_al();

    void *ptr = tda_alloc(&narrow, 32);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_NOT_EQUAL(0, (uintptr_t) ptr % alignof(max_align_t));

    tda_dealloc(&narrow, ptr, 32);
}

static void test_a_list_runs_over_a_narrow_parent() {
    tda_Al narrow = narrow_al();
    tda_Al *al = tda_al_aligned_new(&narrow, alignof(max_align_t));
    TEST_ASSERT_NOT_NULL(al);

    tda_List *l = nullptr;
    TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_list_new(sizeof(long double), al, &l));
    for (int i = 0; i < 8; ++i) {
        const long double val = i;
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_list_push_back(l, &val));
    }

    int i = 0;
    TDA_LIST_FOR_EACH(node, l) {
        const long double *elem = tda_list_node_elem(node);
        TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) elem % alignof(max_align_t));
        TEST_ASSERT_TRUE(*elem == i);
        ++i;
    }
    TEST_ASSERT_EQUAL_INT(8, i);

    tda_list_drop(l);
    tda_al_aligned_drop(al);
}

static void test_an_hmap_runs_over_a_narrow_parent() {
    tda_Al narrow = narrow_al();
    tda_Al *al = tda_al_aligned_new(&narrow, alignof(max_align_t));
    TEST_ASSERT_NOT_NULL(al);

    tda_HMap *m = nullptr;
    TEST_ASSERT_EQUAL(
        TDA_STATUS_OK,
        tda_hmap_new(sizeof(int32_t), sizeof(long double), tda_hash_i32, tda_eq_i32, al, &m)
    );
    for (int32_t key = 0; key < 40; ++key) {
        const long double val = key;
        bool is_new;
        TEST_ASSERT_EQUAL(TDA_STATUS_OK, tda_hmap_insert(m, &key, &val, &is_new));
        TEST_ASSERT_TRUE(is_new);
    }

    for (int32_t key = 0; key < 40; ++key) {
        const long double *val = tda_hmap_get(m, &key);
        TEST_ASSERT_NOT_NULL(val);
        TEST_ASSERT_EQUAL_size_t(0, (uintptr_t) val % alignof(max_align_t));
        TEST_ASSERT_TRUE(*val == key);
    }

    tda_hmap_drop(m);
    tda_al_aligned_drop(al);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_new_takes_two_blocks_and_drop_returns_them);
    RUN_TEST(test_new_cleans_up_after_a_failing_parent);
    RUN_TEST(test_drop_null_is_noop);

    RUN_TEST(test_every_block_is_aligned);
    RUN_TEST(test_blocks_do_not_overlap);
    RUN_TEST(test_the_parent_sees_the_padded_size_both_ways);
    RUN_TEST(test_alloc_zero_is_null_and_asks_the_parent_nothing);
    RUN_TEST(test_alloc_rejects_a_size_the_padding_would_overflow);
    RUN_TEST(test_alloc_fails_when_the_parent_does);

    RUN_TEST(test_dealloc_null_is_noop);

    RUN_TEST(test_calloc_zeroes_an_aligned_block_without_the_parents_calloc);
    RUN_TEST(test_realloc_keeps_the_bytes_and_the_alignment);
    RUN_TEST(test_realloc_to_zero_gives_the_block_back);

    RUN_TEST(test_over_an_arena);
    RUN_TEST(test_over_itself);

    RUN_TEST(test_the_narrow_parent_misses_max_align_t);
    RUN_TEST(test_a_list_runs_over_a_narrow_parent);
    RUN_TEST(test_an_hmap_runs_over_a_narrow_parent);

    return UNITY_END();
}
