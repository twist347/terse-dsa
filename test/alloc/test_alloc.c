#include "tda/alloc/alloc.h"

#include "support/probe.h"

#include <unity.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ========== probe allocator ==========
 *
 * Two flavours of the shared probe (test/support/probe.h): a "bare" one exposing
 * only alloc/dealloc, which forces tda_calloc/tda_realloc down their fallback
 * paths, and a "full" one whose own calloc/realloc must be preferred over those.
 */

static tda_TestProbe probe;

static tda_Al bare_al() {
    return tda_test_probe_bare(&probe);
}

static tda_Al full_al() {
    return tda_test_probe_full(&probe);
}

void setUp() {
    tda_test_probe_reset(&probe);
}

void tearDown() {
}

/* ========== alloc ========== */

static void test_alloc_returns_usable_memory() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc(&al, 32);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(32, probe.last_alloc_size);

    memset(p, 0xAB, 32);
    TEST_ASSERT_EQUAL_UINT8(0xAB, p[0]);
    TEST_ASSERT_EQUAL_UINT8(0xAB, p[31]);

    tda_dealloc(&al, p, 32);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a zero-size request is defined as nullptr and must not reach the allocator
static void test_alloc_zero_is_null_and_does_not_dispatch() {
    tda_Al al = bare_al();

    TEST_ASSERT_NULL(tda_alloc(&al, 0));
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);
}

static void test_alloc_propagates_failure() {
    tda_Al al = bare_al();
    probe.fail_after = 0;

    TEST_ASSERT_NULL(tda_alloc(&al, 16));
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
}

/* ========== dealloc ========== */

static void test_dealloc_null_does_not_dispatch() {
    tda_Al al = bare_al();

    tda_dealloc(&al, nullptr, 16);
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);
}

// the size travels through untouched — the allocator is entitled to rely on it
static void test_dealloc_forwards_the_size() {
    tda_Al al = bare_al();

    void *p = tda_alloc(&al, 24);
    tda_dealloc(&al, p, 24);

    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(24, probe.last_dealloc_size);
}

/* ========== calloc ========== */

static void test_calloc_zero_operand_is_null_and_does_not_dispatch() {
    tda_Al al = full_al();

    TEST_ASSERT_NULL(tda_calloc(&al, 0, 4));
    TEST_ASSERT_NULL(tda_calloc(&al, 4, 0));
    TEST_ASSERT_EQUAL_size_t(0, probe.calloc_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);
}

// with a native calloc present, the fallback must not be used
static void test_calloc_prefers_the_native_hook() {
    tda_Al al = full_al();

    unsigned char *p = tda_calloc(&al, 4, 8);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.calloc_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);

    tda_dealloc(&al, p, 32);
}

// without one, tda_calloc synthesizes it from alloc + memset
static void test_calloc_fallback_zeroes_the_block() {
    tda_Al al = bare_al();

    unsigned char *p = tda_calloc(&al, 4, 8);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(32, probe.last_alloc_size);

    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_dealloc(&al, p, 32);
}

// num * size overflowing size_t is caught above the interface, on both paths: a hook is
// never handed a request that cannot exist, whether or not it brings its own calloc
static void test_calloc_rejects_overflow_on_either_path() {
    tda_Al bare = bare_al();
    TEST_ASSERT_NULL(tda_calloc(&bare, SIZE_MAX, 2));
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);

    tda_Al full = full_al();
    TEST_ASSERT_NULL(tda_calloc(&full, SIZE_MAX, 2));
    TEST_ASSERT_EQUAL_size_t(0, probe.calloc_calls);
}

static void test_calloc_fallback_propagates_failure() {
    tda_Al al = bare_al();
    probe.fail_after = 0;

    TEST_ASSERT_NULL(tda_calloc(&al, 4, 8));
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
}

/* ========== realloc ========== */

static void test_realloc_prefers_the_native_hook() {
    tda_Al al = full_al();

    void *p = tda_alloc(&al, 16);
    void *q = tda_realloc(&al, p, 16, 32);

    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(1, probe.realloc_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);

    free(q);
    probe.live = 0;
}

// new_size == 0 means "release it" — defined behaviour, not a failure
static void test_realloc_to_zero_releases() {
    tda_Al al = bare_al();

    void *p = tda_alloc(&al, 16);
    TEST_ASSERT_NULL(tda_realloc(&al, p, 16, 0));

    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(16, probe.last_dealloc_size);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

static void test_realloc_fallback_grows_and_keeps_the_contents() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc(&al, 8);
    for (size_t i = 0; i < 8; ++i) {
        p[i] = (unsigned char) (i + 1);
    }

    unsigned char *q = tda_realloc(&al, p, 8, 32);
    TEST_ASSERT_NOT_NULL(q);

    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), q[i]);
    }

    // the old block is handed back with its original size
    TEST_ASSERT_EQUAL_size_t(1, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(8, probe.last_dealloc_size);
    TEST_ASSERT_EQUAL_size_t(1, probe.live);

    tda_dealloc(&al, q, 32);
}

// shrinking copies only what fits, and must not read past the old block
static void test_realloc_fallback_shrinks() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc(&al, 16);
    for (size_t i = 0; i < 16; ++i) {
        p[i] = (unsigned char) (i + 1);
    }

    unsigned char *q = tda_realloc(&al, p, 16, 4);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(4, probe.last_alloc_size);

    for (size_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), q[i]);
    }

    tda_dealloc(&al, q, 4);
}

// growing from nothing is just an allocation — there is nothing to release
static void test_realloc_fallback_from_null_is_an_alloc() {
    tda_Al al = bare_al();

    void *p = tda_realloc(&al, nullptr, 0, 16);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);

    tda_dealloc(&al, p, 16);
}

// on failure the original block must survive intact — the caller still owns it
static void test_realloc_fallback_failure_keeps_the_original() {
    tda_Al al = bare_al();

    unsigned char *p = tda_alloc(&al, 8);
    for (size_t i = 0; i < 8; ++i) {
        p[i] = (unsigned char) (i + 1);
    }

    probe.fail_after = 1; // the first alloc already happened
    TEST_ASSERT_NULL(tda_realloc(&al, p, 8, 64));

    TEST_ASSERT_EQUAL_size_t(0, probe.dealloc_calls);
    TEST_ASSERT_EQUAL_size_t(1, probe.live);
    for (size_t i = 0; i < 8; ++i) {
        TEST_ASSERT_EQUAL_UINT8((unsigned char) (i + 1), p[i]);
    }

    probe.fail_after = SIZE_MAX;
    tda_dealloc(&al, p, 8);
}

/* ========== macros ==========
 *
 * These are the only cases that expand TDA_ALLOC and friends. Nothing else in the
 * project uses them, so without these the preprocessor never reads the macro bodies
 * and a broken one ships behind a green build.
 */

static void test_macro_alloc_scales_the_count_by_elem_size() {
    tda_Al al = bare_al();

    int32_t *p = TDA_ALLOC(int32_t, &al, 4);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_alloc_size);

    p[0] = 1;
    p[3] = 4;
    TEST_ASSERT_EQUAL_INT32(1, p[0]);
    TEST_ASSERT_EQUAL_INT32(4, p[3]);

    TDA_DEALLOC(int32_t, &al, p, 4);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_dealloc_size);
    TEST_ASSERT_EQUAL_size_t(0, probe.live);
}

// a count with a side effect is taken once, so the size handed to TDA_DEALLOC later is
// the size that was allocated
static void test_macro_alloc_reads_the_count_once() {
    tda_Al al = bare_al();

    size_t n = 4;
    int32_t *p = TDA_ALLOC(int32_t, &al, n++);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(5, n);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_alloc_size);

    TDA_DEALLOC(int32_t, &al, p, 4);
}

// count * sizeof(T) would wrap: the request is refused before it reaches the
// allocator, which would otherwise be asked for a buffer smaller than the caller wants
static void test_macro_alloc_rejects_a_count_that_would_wrap() {
    tda_Al al = bare_al();

    TEST_ASSERT_NULL(TDA_ALLOC(int32_t, &al, SIZE_MAX / sizeof(int32_t) + 2));
    TEST_ASSERT_EQUAL_size_t(0, probe.alloc_calls);
}

// the largest count that still fits is not the guard's business: it goes through and
// fails as an ordinary out-of-memory
static void test_macro_alloc_passes_the_largest_fitting_count_through() {
    tda_Al al = bare_al();

    TEST_ASSERT_NULL(TDA_ALLOC(int32_t, &al, SIZE_MAX / sizeof(int32_t)));
    TEST_ASSERT_EQUAL_size_t(1, probe.alloc_calls);
}

static void test_macro_calloc_hands_the_operands_over_unmultiplied() {
    tda_Al al = full_al();

    int32_t *p = TDA_CALLOC(int32_t, &al, 4);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_size_t(1, probe.calloc_calls);
    TEST_ASSERT_EQUAL_INT32(0, p[0]);
    TEST_ASSERT_EQUAL_INT32(0, p[3]);

    TDA_DEALLOC(int32_t, &al, p, 4);
}

// the count is never multiplied by the macro, so the overflow is tda_calloc's to catch
// and TDA_CALLOC needs no guard of its own — see test_calloc_rejects_overflow_on_either_path
// for the contract this leans on.
static void test_macro_calloc_overflow_is_caught_below_the_macro() {
    tda_Al al = bare_al();

    TEST_ASSERT_NULL(TDA_CALLOC(int32_t, &al, SIZE_MAX));
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
    TEST_ASSERT_EQUAL_size_t(8 * sizeof(int32_t), probe.last_alloc_size);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_dealloc_size);

    for (int32_t i = 0; i < 4; ++i) {
        TEST_ASSERT_EQUAL_INT32(i + 1, q[i]);
    }

    TDA_DEALLOC(int32_t, &al, q, 8);
}

static void test_macro_realloc_reads_each_count_once() {
    tda_Al al = bare_al();

    int32_t *p = TDA_ALLOC(int32_t, &al, 4);

    size_t old_n = 4;
    size_t new_n = 8;
    int32_t *q = TDA_REALLOC(int32_t, &al, p, old_n++, new_n++);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_size_t(5, old_n);
    TEST_ASSERT_EQUAL_size_t(9, new_n);
    TEST_ASSERT_EQUAL_size_t(8 * sizeof(int32_t), probe.last_alloc_size);
    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), probe.last_dealloc_size);

    TDA_DEALLOC(int32_t, &al, q, 8);
}

// new_count * sizeof(T) wraps to exactly 0 here. Unguarded that reaches tda_realloc as
// "resize to nothing", which releases the block and reports nullptr — and the caller,
// told its pointer survives a failure, is left holding freed memory.
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

    RUN_TEST(test_alloc_returns_usable_memory);
    RUN_TEST(test_alloc_zero_is_null_and_does_not_dispatch);
    RUN_TEST(test_alloc_propagates_failure);

    RUN_TEST(test_dealloc_null_does_not_dispatch);
    RUN_TEST(test_dealloc_forwards_the_size);

    RUN_TEST(test_calloc_zero_operand_is_null_and_does_not_dispatch);
    RUN_TEST(test_calloc_prefers_the_native_hook);
    RUN_TEST(test_calloc_fallback_zeroes_the_block);
    RUN_TEST(test_calloc_rejects_overflow_on_either_path);
    RUN_TEST(test_calloc_fallback_propagates_failure);

    RUN_TEST(test_realloc_prefers_the_native_hook);
    RUN_TEST(test_realloc_to_zero_releases);
    RUN_TEST(test_realloc_fallback_grows_and_keeps_the_contents);
    RUN_TEST(test_realloc_fallback_shrinks);
    RUN_TEST(test_realloc_fallback_from_null_is_an_alloc);
    RUN_TEST(test_realloc_fallback_failure_keeps_the_original);

    RUN_TEST(test_macro_alloc_scales_the_count_by_elem_size);
    RUN_TEST(test_macro_alloc_reads_the_count_once);
    RUN_TEST(test_macro_alloc_rejects_a_count_that_would_wrap);
    RUN_TEST(test_macro_alloc_passes_the_largest_fitting_count_through);
    RUN_TEST(test_macro_calloc_hands_the_operands_over_unmultiplied);
    RUN_TEST(test_macro_calloc_overflow_is_caught_below_the_macro);
    RUN_TEST(test_macro_realloc_scales_both_counts);
    RUN_TEST(test_macro_realloc_reads_each_count_once);
    RUN_TEST(test_macro_realloc_rejects_a_new_count_that_would_wrap);

    return UNITY_END();
}
