#include "tda/alloc/default.h"

#include "support/layout.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

static tda_Layout bytes(size_t size) {
    return (tda_Layout){.size = size, .align = alignof(max_align_t)};
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

/* ========== identity ========== */

// the default allocator is a static singleton: every call returns the same object
static void test_default_is_a_singleton() {
    const tda_Al *a = tda_al_default();
    const tda_Al *b = tda_al_default();

    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_PTR(a, b);
}

// libc has calloc and realloc but no call that grows a block only in place
static void test_default_has_every_hook_but_resize() {
    const tda_Al *al = tda_al_default();

    TEST_ASSERT_NOT_NULL(al->alloc);
    TEST_ASSERT_NOT_NULL(al->alloc_zeroed);
    TEST_ASSERT_NULL(al->resize);
    TEST_ASSERT_NOT_NULL(al->remap);
    TEST_ASSERT_NOT_NULL(al->dealloc);
}

/* ========== alloc ========== */

static void test_alloc_returns_usable_memory() {
    tda_Al *al = tda_al_default();

    unsigned char *p = tda_alloc(al, bytes(64));
    TEST_ASSERT_NOT_NULL(p);

    memset(p, 0x5A, 64);
    TEST_ASSERT_EQUAL_UINT8(0x5A, p[0]);
    TEST_ASSERT_EQUAL_UINT8(0x5A, p[63]);

    tda_dealloc(al, p, bytes(64));
}

static void test_alloc_zero_is_null() {
    TEST_ASSERT_NULL(tda_alloc(tda_al_default(), bytes(0)));
}

// every alignment, the plain ones from malloc and the larger ones past it, for sizes
// both under and over the alignment
static void test_alloc_gives_the_alignment_asked_for() {
    tda_Al *al = tda_al_default();

    for (size_t align = 1; align <= 4096; align *= 2) {
        for (size_t size = 1; size <= 2 * align; size *= 2) {
            void *p = tda_alloc(al, tda_test_layout(size, align));
            TEST_ASSERT_NOT_NULL(p);
            TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % align);
            memset(p, 0xEE, size);
            tda_dealloc(al, p, tda_test_layout(size, align));
        }
    }
}

// two live allocations are distinct regions: writing one must not touch the other
static void test_allocations_are_independent() {
    tda_Al *al = tda_al_default();

    unsigned char *a = tda_alloc(al, bytes(16));
    unsigned char *b = tda_alloc(al, bytes(16));
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_TRUE(a != b);

    memset(a, 0x11, 16);
    memset(b, 0x22, 16);
    TEST_ASSERT_EQUAL_UINT8(0x11, a[0]);
    TEST_ASSERT_EQUAL_UINT8(0x22, b[0]);

    tda_dealloc(al, b, bytes(16));
    tda_dealloc(al, a, bytes(16));
}

/* ========== alloc_zeroed ========== */

static void test_alloc_zeroed_zeroes_the_block() {
    tda_Al *al = tda_al_default();

    unsigned char *p = tda_alloc_zeroed(al, bytes(32));
    TEST_ASSERT_NOT_NULL(p);

    for (size_t i = 0; i < 32; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_dealloc(al, p, bytes(32));
}

// past malloc's alignment there is no calloc, so it is aligned_alloc and a memset
static void test_alloc_zeroed_zeroes_an_over_aligned_block() {
    tda_Al *al = tda_al_default();

    unsigned char *p = tda_alloc_zeroed(al, tda_test_layout(300, 256));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % 256);

    for (size_t i = 0; i < 300; ++i) {
        TEST_ASSERT_EQUAL_UINT8(0, p[i]);
    }

    tda_dealloc(al, p, tda_test_layout(300, 256));
}

static void test_alloc_zeroed_zero_is_null() {
    TEST_ASSERT_NULL(tda_alloc_zeroed(tda_al_default(), bytes(0)));
}

/* ========== realloc ========== */

static void test_realloc_grows_and_keeps_the_contents() {
    tda_Al *al = tda_al_default();

    unsigned char *p = tda_alloc(al, bytes(8));
    TEST_ASSERT_NOT_NULL(p);
    fill_ramp(p, 8);

    unsigned char *q = tda_realloc(al, p, bytes(8), 64);
    TEST_ASSERT_NOT_NULL(q);
    assert_ramp(q, 8);

    tda_dealloc(al, q, bytes(64));
}

static void test_realloc_shrinks_and_keeps_the_prefix() {
    tda_Al *al = tda_al_default();

    unsigned char *p = tda_alloc(al, bytes(32));
    TEST_ASSERT_NOT_NULL(p);
    fill_ramp(p, 32);

    unsigned char *q = tda_realloc(al, p, bytes(32), 4);
    TEST_ASSERT_NOT_NULL(q);
    assert_ramp(q, 4);

    tda_dealloc(al, q, bytes(4));
}

// realloc keeps only malloc's alignment, so remap declines an over-aligned block and the
// wrapper moves it by a copy that keeps the alignment
static void test_realloc_keeps_an_over_aligned_block_aligned() {
    tda_Al *al = tda_al_default();

    unsigned char *p = tda_alloc(al, tda_test_layout(256, 256));
    TEST_ASSERT_NOT_NULL(p);
    fill_ramp(p, 256);
    TEST_ASSERT_NULL(tda_remap(al, p, tda_test_layout(256, 256), 4096));

    unsigned char *q = tda_realloc(al, p, tda_test_layout(256, 256), 4096);
    TEST_ASSERT_NOT_NULL(q);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) q % 256);
    assert_ramp(q, 256);

    tda_dealloc(al, q, tda_test_layout(4096, 256));
}

// C23 owes malloc(n) the alignment of types of size n or less, so a block shorter than
// its alignment still comes back aligned, however it was made or resized
static void test_a_block_shorter_than_its_alignment_is_aligned_anyway() {
    tda_Al *al = tda_al_default();
    constexpr size_t max = alignof(max_align_t);

    // the small ones too: some aligned_alloc refuse an alignment under a pointer's
    for (size_t align = 1; align <= max; align *= 2) {
        unsigned char *p = tda_alloc(al, tda_test_layout(1, align));
        TEST_ASSERT_NOT_NULL(p);
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % align);
        p[0] = 7;

        unsigned char *q = tda_realloc(al, p, tda_test_layout(1, align), 3 * max);
        TEST_ASSERT_NOT_NULL(q);
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) q % align);
        TEST_ASSERT_EQUAL_UINT8(7, q[0]);

        unsigned char *r = tda_realloc(al, q, tda_test_layout(3 * max, align), 1);
        TEST_ASSERT_NOT_NULL(r);
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) r % align);
        TEST_ASSERT_EQUAL_UINT8(7, r[0]);
        tda_dealloc(al, r, tda_test_layout(1, align));

        unsigned char *z = tda_alloc_zeroed(al, tda_test_layout(1, align));
        TEST_ASSERT_NOT_NULL(z);
        TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) z % align);
        TEST_ASSERT_EQUAL_UINT8(0, z[0]);
        tda_dealloc(al, z, tda_test_layout(1, align));
    }
}

static void test_realloc_to_zero_releases() {
    tda_Al *al = tda_al_default();

    void *p = tda_alloc(al, bytes(16));
    TEST_ASSERT_NOT_NULL(p);

    TEST_ASSERT_NULL(tda_realloc(al, p, bytes(16), 0));
}

static void test_realloc_from_null_is_an_alloc() {
    tda_Al *al = tda_al_default();

    void *p = tda_realloc(al, nullptr, tda_test_layout(0, 128), 16);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0, (uintptr_t) p % 128);

    tda_dealloc(al, p, tda_test_layout(16, 128));
}

/* ========== dealloc ========== */

static void test_dealloc_null_is_noop() {
    tda_dealloc(tda_al_default(), nullptr, bytes(16));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_default_is_a_singleton);
    RUN_TEST(test_default_has_every_hook_but_resize);

    RUN_TEST(test_alloc_returns_usable_memory);
    RUN_TEST(test_alloc_zero_is_null);
    RUN_TEST(test_alloc_gives_the_alignment_asked_for);
    RUN_TEST(test_allocations_are_independent);

    RUN_TEST(test_alloc_zeroed_zeroes_the_block);
    RUN_TEST(test_alloc_zeroed_zeroes_an_over_aligned_block);
    RUN_TEST(test_alloc_zeroed_zero_is_null);

    RUN_TEST(test_realloc_grows_and_keeps_the_contents);
    RUN_TEST(test_realloc_shrinks_and_keeps_the_prefix);
    RUN_TEST(test_realloc_keeps_an_over_aligned_block_aligned);
    RUN_TEST(test_a_block_shorter_than_its_alignment_is_aligned_anyway);
    RUN_TEST(test_realloc_to_zero_releases);
    RUN_TEST(test_realloc_from_null_is_an_alloc);

    RUN_TEST(test_dealloc_null_is_noop);

    return UNITY_END();
}
