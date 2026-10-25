#include "tda/alloc/alloc.h"

#include "support/probe.h"

#include <unity.h>

#include <stdint.h>
#include <string.h>

/* ========== probe allocator ==========
 *
 * Two flavours of the shared probe (test/support/probe.h): a "bare" one with only alloc
 * and dealloc, which drives every wrapper down its fallback, and a "full" one whose own
 * hooks must be preferred over those. The full probe shrinks in place and never grows,
 * so a growth reaches its remap.
 */

static tda_TestProbe probe;

static tda_Al bare_al() {
    return tda_test_probe_bare(&probe);
}

static tda_Al full_al() {
    return tda_test_probe_full(&probe);
}

static tda_Layout bytes(size_t size) {
    return (tda_Layout){.size = size, .align = alignof(max_align_t)};
}

// past malloc's own alignment, so a block of it cannot come from the plain calls
static constexpr size_t WIDE = 64;

static tda_Layout wide(size_t size) {
    return (tda_Layout){.size = size, .align = WIDE};
}

static void fill_ramp(unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        p[i] = (unsigned char) (i + 1);
    }
}

static void assert_ramp(const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), p[i]);
    }
}

void setUp() {
    tda_test_probe_reset(&probe);
}

void tearDown() {
}

/* ========== alloc ========== */

static void test_alloc_hands_the_layout_over_and_returns_usable_memory() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc(&al, wide(32));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(32, probe.last_alloc.size);
    TEST_ASSERT_EQUAL_size_t(WIDE, probe.last_alloc.align);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % WIDE);

    memset(p, 0xAB, 32);
    TEST_ASSERT_EQUAL_UINT8(0xAB, p[31]);

    tda_dealloc(&al, p, wide(32));
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a zero-size request is defined as nullptr and must not reach the allocator
static void test_alloc_zero_is_null_and_does_not_dispatch() {
    tda_Al al = bare_al();

    TEST_ASSERT_NULL(tda_alloc(&al, bytes(0)));
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);
}

static void test_alloc_propagates_failure() {
    tda_Al al = bare_al();
    probe.fail_after = 0;

    TEST_ASSERT_NULL(tda_alloc(&al, bytes(16)));
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
}

/* ========== dealloc ========== */

static void test_dealloc_null_does_not_dispatch() {
    tda_Al al = bare_al();

    tda_dealloc(&al, nullptr, bytes(16));
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);
}

// the layout reaches the allocator unchanged, and the allocator may rely on it
static void test_dealloc_forwards_the_layout() {
    tda_Al al = bare_al();

    void *p = tda_alloc(&al, wide(24));
    tda_dealloc(&al, p, wide(24));

    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(24, probe.last_dealloc.size);
    TEST_ASSERT_EQUAL_size_t(WIDE, probe.last_dealloc.align);
}

/* ========== alloc_zeroed ========== */

static void test_alloc_zeroed_zero_is_null_and_does_not_dispatch() {
    tda_Al al = full_al();

    TEST_ASSERT_NULL(tda_alloc_zeroed(&al, bytes(0)));
    TEST_ASSERT_EQUAL_size_t(0, probe.zeroed_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);
}

// with a native hook present, the fallback must not be used
static void test_alloc_zeroed_prefers_the_native_hook() {
    tda_Al al = full_al();

    unsigned char *p = tda_alloc_zeroed(&al, bytes(32));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.zeroed_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);
    TEST_ASSERT_EQUAL_UINT8(0, p[31]);

    tda_dealloc(&al, p, bytes(32));
}

// without one, the wrapper builds it from alloc and memset, alignment kept
static void test_alloc_zeroed_fallback_zeroes_the_block() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc_zeroed(&al, wide(32));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(WIDE, probe.last_alloc.align);

    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_dealloc(&al, p, wide(32));
}

static void test_alloc_zeroed_fallback_propagates_failure() {
    tda_Al al = bare_al();
    probe.fail_after = 0;

    TEST_ASSERT_NULL(tda_alloc_zeroed(&al, bytes(32)));
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
}

/* ========== resize and remap ========== */

// a missing hook means never, and the wrapper answers without dispatching
static void test_resize_and_remap_without_hooks_decline() {
    tda_Al al = bare_al();

    void *p = tda_alloc(&al, bytes(16));
    TEST_ASSERT_FALSE(tda_resize(&al, p, bytes(16), 8));
    TEST_ASSERT_NULL(tda_remap(&al, p, bytes(16), 32));
    TEST_ASSERT_EQUAL_size_t(1, probe.live);

    tda_dealloc(&al, p, bytes(16));
}

static void test_resize_and_remap_pass_the_hooks_answers_on() {
    tda_Al al = full_al();

    void *p = tda_alloc(&al, bytes(16));
    TEST_ASSERT_TRUE(tda_resize(&al, p, bytes(16), 8));
    TEST_ASSERT_FALSE(tda_resize(&al, p, bytes(16), 32));
    TEST_ASSERT_EQUAL_size_t(2, probe.resize_calls);

    void *q = tda_remap(&al, p, bytes(16), 32);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(1, probe.remap_calls);

    tda_dealloc(&al, q, bytes(32));
}

/* ========== realloc ========== */

// the cheapest way first: a block that resizes in place is not moved or copied
static void test_realloc_tries_resize_first() {
    tda_Al al = full_al();

    void *p = tda_alloc(&al, bytes(32));
    void *q = tda_realloc(&al, p, bytes(32), 16);

    TEST_ASSERT_EQUAL_PTR(p, q);
    TEST_ASSERT_EQUAL_size_t(1, probe.resize_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.remap_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);

    tda_dealloc(&al, q, bytes(16));
}

// then the allocator's own move, before any copy of the wrapper's
static void test_realloc_tries_remap_second() {
    tda_Al al = full_al();

    // as long as its alignment: under that, default owes it no plain realloc
    unsigned char *p = tda_alloc(&al, bytes(16));
    fill_ramp(p, 16);
    unsigned char *q = tda_realloc(&al, p, bytes(16), 64);

    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(1, probe.resize_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.remap_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);
    assert_ramp(q, 16);

    tda_dealloc(&al, q, bytes(64));
}

// new_size == 0 releases the block; this is defined behaviour, not a failure
static void test_realloc_to_zero_releases() {
    tda_Al al = bare_al();

    void *p = tda_alloc(&al, bytes(16));
    TEST_ASSERT_NULL(tda_realloc(&al, p, bytes(16), 0));

    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(16, probe.last_dealloc.size);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_realloc_fallback_grows_and_keeps_the_contents() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc(&al, bytes(8));
    fill_ramp(p, 8);

    unsigned char *q = tda_realloc(&al, p, bytes(8), 32);
    TEST_ASSERT_NOT_NULL(q);
    assert_ramp(q, 8);

    // the old block is handed back with its original layout
    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(8, probe.last_dealloc.size);
    TEST_ASSERT_EQUAL_size_t(1, probe.live);

    tda_dealloc(&al, q, bytes(32));
}

// a block that moves by a copy keeps the alignment it was made with
static void test_realloc_fallback_keeps_the_alignment() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc(&al, wide(8));
    fill_ramp(p, 8);

    unsigned char *q = tda_realloc(&al, p, wide(8), 256);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(WIDE, probe.last_alloc.align);
    TEST_ASSERT_EQUAL_size_t(WIDE, probe.last_dealloc.align);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) q % WIDE);
    assert_ramp(q, 8);

    tda_dealloc(&al, q, wide(256));
}

// shrinking copies only what fits, and must not read past the old block
static void test_realloc_fallback_shrinks() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc(&al, bytes(16));
    fill_ramp(p, 16);

    unsigned char *q = tda_realloc(&al, p, bytes(16), 4);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(4, probe.last_alloc.size);
    assert_ramp(q, 4);

    tda_dealloc(&al, q, bytes(4));
}

// growing from nothing is a plain allocation at the alignment asked for
static void test_realloc_from_null_is_an_alloc() {
    tda_Al al = full_al();

    void *p = tda_realloc(&al, nullptr, wide(0), 16);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(WIDE, probe.last_alloc.align);
    TEST_ASSERT_EQUAL_size_t(0, probe.resize_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);

    tda_dealloc(&al, p, wide(16));
}

// on failure the original block stays intact; the caller still owns it
static void test_realloc_failure_keeps_the_original() {
    tda_Al bare = bare_al();

    unsigned char *p = tda_alloc(&bare, bytes(8));
    fill_ramp(p, 8);

    probe.fail_after = 1; // the first alloc already happened
    TEST_ASSERT_NULL(tda_realloc(&bare, p, bytes(8), 64));

    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.live);
    assert_ramp(p, 8);

    probe.fail_after = SIZE_MAX;
    tda_dealloc(&bare, p, bytes(8));
}

// with every hook refusing, remap's null is not the end: the copy is tried, and its
// failure too leaves the block where it was
static void test_realloc_falls_past_a_refused_remap_to_the_copy() {
    tda_Al full = full_al();

    unsigned char *p = tda_alloc(&full, bytes(8));
    fill_ramp(p, 8);

    tda_test_probe_fail_after_next(&probe, 0);
    TEST_ASSERT_NULL(tda_realloc(&full, p, bytes(8), 64));

    TEST_ASSERT_EQUAL_size_t(1, probe.remap_calls);
    TEST_ASSERT_EQUAL_size_t(2, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.live);
    assert_ramp(p, 8);

    probe.fail_after = SIZE_MAX;
    tda_dealloc(&full, p, bytes(8));
}

/* ========== macros ==========
 *
 * These are the only cases that expand TDA_ALLOC and friends with a probe underneath,
 * so the layout each one builds is checked here and nowhere else.
 */

static void test_macro_alloc_takes_the_layout_from_the_type() {
    tda_Al al = bare_al();

    int32_t *p = TDA_ALLOC(int32_t, &al, 4);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_alloc.size);
    TEST_ASSERT_EQUAL_size_t(alignof(int32_t), probe.last_alloc.align);

    p[0] = 1;
    p[3] = 4;
    TEST_ASSERT_EQUAL_INT32(4, p[3]);

    TDA_DEALLOC(int32_t, &al, p, 4);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_dealloc.size);
    TEST_ASSERT_EQUAL_size_t(alignof(int32_t), probe.last_dealloc.align);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

typedef struct {
    alignas(64) float lane[16];
} Wide;

static void test_macro_alloc_aligns_an_over_aligned_type() {
    tda_Al al = bare_al();

    Wide *p = TDA_ALLOC(Wide, &al, 3);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(64, probe.last_alloc.align);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % 64);

    TDA_DEALLOC(Wide, &al, p, 3);
}

// a count with a side effect is taken once, so the size handed to TDA_DEALLOC later is
// the size that was allocated
static void test_macro_alloc_reads_the_count_once() {
    tda_Al al = bare_al();

    size_t n = 4;
    int32_t *p = TDA_ALLOC(int32_t, &al, n++);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(5, n);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_alloc.size);

    TDA_DEALLOC(int32_t, &al, p, 4);
}

// count * sizeof(T) would wrap, so the request is refused before the allocator is asked
// for a buffer smaller than the caller wants
static void test_macro_alloc_rejects_a_count_that_would_wrap() {
    tda_Al al = bare_al();

    TEST_ASSERT_NULL(TDA_ALLOC(int32_t, &al, SIZE_MAX / sizeof(int32_t) + 2));
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);
}

// the largest count that still fits passes the guard and fails as an ordinary
// out-of-memory error
static void test_macro_alloc_passes_the_largest_fitting_count_through() {
    tda_Al al = bare_al();

    TEST_ASSERT_NULL(TDA_ALLOC(int32_t, &al, SIZE_MAX / sizeof(int32_t)));
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
}

static void test_macro_alloc_zeroed_goes_to_the_native_hook() {
    tda_Al al = full_al();

    int32_t *p = TDA_ALLOC_ZEROED(int32_t, &al, 4);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.zeroed_calls);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_alloc.size);
    TEST_ASSERT_EQUAL_INT32(0, p[3]);

    TDA_DEALLOC(int32_t, &al, p, 4);
}

static void test_macro_alloc_zeroed_rejects_a_count_that_would_wrap() {
    tda_Al al = full_al();

    TEST_ASSERT_NULL(TDA_ALLOC_ZEROED(int32_t, &al, SIZE_MAX));
    TEST_ASSERT_EQUAL_size_t(0, probe.zeroed_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);
}

static void test_macro_realloc_scales_both_counts() {
    tda_Al al = bare_al();

    int32_t *p = TDA_ALLOC(int32_t, &al, 4);
    for (int32_t i = 0; i < 4; ++i) {
        p[i] = i + 1;
    }

    int32_t *q = TDA_REALLOC(int32_t, &al, p, 4, 8);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(8 * sizeof(int32_t), probe.last_alloc.size);
    TEST_ASSERT_EQUAL_size_t(alignof(int32_t), probe.last_alloc.align);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_dealloc.size);

    for (int32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT32(i + 1, q[i]);
    }

    TDA_DEALLOC(int32_t, &al, q, 8);
}

static void test_macro_realloc_reads_each_count_once() {
    tda_Al al = bare_al();

    int32_t *p = TDA_ALLOC(int32_t, &al, 4);

    size_t old_n = 4, new_n = 8;
    int32_t *q = TDA_REALLOC(int32_t, &al, p, old_n++, new_n++);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(5, old_n);
    TEST_ASSERT_EQUAL_size_t(9, new_n);
    TEST_ASSERT_EQUAL_size_t(8 * sizeof(int32_t), probe.last_alloc.size);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_dealloc.size);

    TDA_DEALLOC(int32_t, &al, q, 8);
}

// new_count * sizeof(T) wraps to exactly 0 here. Unguarded, tda_realloc would take that
// as a release and return nullptr, and the caller, told its pointer survives a failure,
// would be left holding freed memory.
static void test_macro_realloc_rejects_a_new_count_that_would_wrap() {
    tda_Al al = bare_al();

    int32_t *p = TDA_ALLOC(int32_t, &al, 4);
    for (int32_t i = 0; i < 4; ++i) {
        p[i] = i + 1;
    }

    TEST_ASSERT_NULL(TDA_REALLOC(int32_t, &al, p, 4, (size_t) 1 << 62));

    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.live);
    for (int32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT32(i + 1, p[i]);
    }

    TDA_DEALLOC(int32_t, &al, p, 4);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_alloc_hands_the_layout_over_and_returns_usable_memory);
    RUN_TEST(test_alloc_zero_is_null_and_does_not_dispatch);
    RUN_TEST(test_alloc_propagates_failure);

    RUN_TEST(test_dealloc_null_does_not_dispatch);
    RUN_TEST(test_dealloc_forwards_the_layout);

    RUN_TEST(test_alloc_zeroed_zero_is_null_and_does_not_dispatch);
    RUN_TEST(test_alloc_zeroed_prefers_the_native_hook);
    RUN_TEST(test_alloc_zeroed_fallback_zeroes_the_block);
    RUN_TEST(test_alloc_zeroed_fallback_propagates_failure);

    RUN_TEST(test_resize_and_remap_without_hooks_decline);
    RUN_TEST(test_resize_and_remap_pass_the_hooks_answers_on);

    RUN_TEST(test_realloc_tries_resize_first);
    RUN_TEST(test_realloc_tries_remap_second);
    RUN_TEST(test_realloc_to_zero_releases);
    RUN_TEST(test_realloc_fallback_grows_and_keeps_the_contents);
    RUN_TEST(test_realloc_fallback_keeps_the_alignment);
    RUN_TEST(test_realloc_fallback_shrinks);
    RUN_TEST(test_realloc_from_null_is_an_alloc);
    RUN_TEST(test_realloc_failure_keeps_the_original);
    RUN_TEST(test_realloc_falls_past_a_refused_remap_to_the_copy);

    RUN_TEST(test_macro_alloc_takes_the_layout_from_the_type);
    RUN_TEST(test_macro_alloc_aligns_an_over_aligned_type);
    RUN_TEST(test_macro_alloc_reads_the_count_once);
    RUN_TEST(test_macro_alloc_rejects_a_count_that_would_wrap);
    RUN_TEST(test_macro_alloc_passes_the_largest_fitting_count_through);
    RUN_TEST(test_macro_alloc_zeroed_goes_to_the_native_hook);
    RUN_TEST(test_macro_alloc_zeroed_rejects_a_count_that_would_wrap);
    RUN_TEST(test_macro_realloc_scales_both_counts);
    RUN_TEST(test_macro_realloc_reads_each_count_once);
    RUN_TEST(test_macro_realloc_rejects_a_new_count_that_would_wrap);

    return UNITY_END();
}
