#include "tda/core/align.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

/* ========== sizes ========== */

static void test_align_up_rounds_to_the_next_multiple() {
    TEST_ASSERT_EQUAL_size_t(0, tda_align_up(0, 8));
    TEST_ASSERT_EQUAL_size_t(8, tda_align_up(1, 8));
    TEST_ASSERT_EQUAL_size_t(8, tda_align_up(8, 8));
    TEST_ASSERT_EQUAL_size_t(128, tda_align_up(100, 64));
    // an alignment of 1 changes nothing
    TEST_ASSERT_EQUAL_size_t(7, tda_align_up(7, 1));
}

static void test_ckd_up_rounds_as_align_up_does() {
    size_t out = 0;
    TEST_ASSERT_FALSE(tda_align_ckd_up(&out, 100, 64));
    TEST_ASSERT_EQUAL_size_t(128, out);

    // the largest multiple still fits
    TEST_ASSERT_FALSE(tda_align_ckd_up(&out, SIZE_MAX - 15, 16));
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX - 15, out);
}

static void test_ckd_up_reports_an_overflow_and_leaves_out() {
    size_t out = 42;
    TEST_ASSERT_TRUE(tda_align_ckd_up(&out, SIZE_MAX - 14, 16));
    TEST_ASSERT_EQUAL_size_t(42, out);
}

/* ========== addresses ========== */

static void test_pad_reaches_the_next_boundary() {
    alignas(64) unsigned char buf[128] = {};

    TEST_ASSERT_EQUAL_size_t(0, tda_align_pad(buf, 64));
    TEST_ASSERT_EQUAL_size_t(63, tda_align_pad(buf + 1, 64));
    TEST_ASSERT_EQUAL_size_t(5, tda_align_pad(buf + 3, 8));
    TEST_ASSERT_EQUAL_size_t(0, tda_align_pad(buf + 3, 1));
}

static void test_meets_checks_the_address() {
    alignas(64) unsigned char buf[128] = {};

    TEST_ASSERT_TRUE(tda_align_meets(buf, 64));
    TEST_ASSERT_TRUE(tda_align_meets(buf + 16, 16));
    TEST_ASSERT_FALSE(tda_align_meets(buf + 16, 32));
    TEST_ASSERT_TRUE(tda_align_meets(buf + 3, 1));
}

// what an allocator does with the two: skip the pad, then land on the boundary
static void test_pad_lands_on_what_meets_checks() {
    alignas(64) unsigned char buf[128] = {};

    for (size_t off = 0; off < 64; ++off) {
        const unsigned char *at = buf + off;
        TEST_ASSERT_TRUE(tda_align_meets(at + tda_align_pad(at, 32), 32));
    }
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_align_up_rounds_to_the_next_multiple);
    RUN_TEST(test_ckd_up_rounds_as_align_up_does);
    RUN_TEST(test_ckd_up_reports_an_overflow_and_leaves_out);

    RUN_TEST(test_pad_reaches_the_next_boundary);
    RUN_TEST(test_meets_checks_the_address);
    RUN_TEST(test_pad_lands_on_what_meets_checks);

    return UNITY_END();
}
