#include "tda/core/layout.h"

#include <unity.h>

#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

typedef struct {
    alignas(64) unsigned char lane[64];
} Wide;

typedef struct {
    char c;
    int64_t i;
} Padded;

/* ========== TDA_LAYOUT_OF ========== */

static void test_layout_of_takes_sizeof_and_alignof() {
    const tda_Layout i32 = TDA_LAYOUT_OF(int32_t);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), i32.size);
    TEST_ASSERT_EQUAL_size_t(alignof(int32_t), i32.align);

    const tda_Layout wide = TDA_LAYOUT_OF(Wide);
    TEST_ASSERT_EQUAL_size_t(64, wide.size);
    TEST_ASSERT_EQUAL_size_t(64, wide.align);

    // a struct's padding is in its size, so a type layout always tiles
    const tda_Layout padded = TDA_LAYOUT_OF(Padded);
    TEST_ASSERT_EQUAL_size_t(0, padded.size % padded.align);
}

/* ========== info ========== */

static void test_is_align_takes_powers_of_two_only() {
    for (size_t a = 1; a != 0; a <<= 1) {
        TEST_ASSERT_TRUE(tda_layout_is_align(a));
    }

    TEST_ASSERT_FALSE(tda_layout_is_align(0));
    TEST_ASSERT_FALSE(tda_layout_is_align(3));
    TEST_ASSERT_FALSE(tda_layout_is_align(24));
    TEST_ASSERT_FALSE(tda_layout_is_align(SIZE_MAX));
}

static void test_is_type_wants_a_size_that_is_a_multiple_of_the_alignment() {
    TEST_ASSERT_TRUE(tda_layout_is_type(TDA_LAYOUT_OF(Padded)));
    TEST_ASSERT_TRUE(tda_layout_is_type((tda_Layout){.size = 48, .align = 16}));

    // a fine request, but no type has it
    TEST_ASSERT_FALSE(tda_layout_is_type((tda_Layout){.size = 1, .align = 64}));
    TEST_ASSERT_FALSE(tda_layout_is_type((tda_Layout){.size = 0, .align = 8}));
    TEST_ASSERT_FALSE(tda_layout_is_type((tda_Layout){.size = 12, .align = 6}));
}

// alignment is part of the type: the same size at another alignment is another layout
static void test_eq_compares_size_and_alignment() {
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(Padded), TDA_LAYOUT_OF(Padded)));
    TEST_ASSERT_TRUE(tda_layout_eq(TDA_LAYOUT_OF(uint64_t), (tda_Layout){.size = 8, .align = alignof(uint64_t)}));

    TEST_ASSERT_FALSE(tda_layout_eq((tda_Layout){.size = 8, .align = 8}, (tda_Layout){.size = 8, .align = 4}));
    TEST_ASSERT_FALSE(tda_layout_eq((tda_Layout){.size = 8, .align = 8}, (tda_Layout){.size = 16, .align = 8}));
}

/* ========== build ========== */

static void test_ckd_array_repeats_the_elem_and_keeps_its_alignment() {
    tda_Layout out;
    TEST_ASSERT_FALSE(tda_layout_ckd_array(&out, TDA_LAYOUT_OF(Wide), 3));
    TEST_ASSERT_EQUAL_size_t(3 * 64, out.size);
    TEST_ASSERT_EQUAL_size_t(64, out.align);

    // zero of them is an empty block at the elem's alignment
    TEST_ASSERT_FALSE(tda_layout_ckd_array(&out, TDA_LAYOUT_OF(int32_t), 0));
    TEST_ASSERT_EQUAL_size_t(0, out.size);
    TEST_ASSERT_EQUAL_size_t(alignof(int32_t), out.align);
}

static void test_ckd_array_reports_an_overflow() {
    tda_Layout out;
    TEST_ASSERT_TRUE(tda_layout_ckd_array(&out, TDA_LAYOUT_OF(int32_t), SIZE_MAX / 4 + 1));
    TEST_ASSERT_FALSE(tda_layout_ckd_array(&out, TDA_LAYOUT_OF(int32_t), SIZE_MAX / 4));
    TEST_ASSERT_EQUAL_size_t(SIZE_MAX / 4 * 4, out.size);
}

// the offsets a C struct of the same members gets
static void test_ckd_extend_lays_fields_out_as_a_struct_does() {
    typedef struct {
        char c;
        int64_t i;
        int16_t s;
    } Mixed;

    tda_Layout l = TDA_LAYOUT_OF(char);
    size_t off_i;
    size_t off_s;
    TEST_ASSERT_FALSE(tda_layout_ckd_extend(&l, TDA_LAYOUT_OF(int64_t), &off_i));
    TEST_ASSERT_FALSE(tda_layout_ckd_extend(&l, TDA_LAYOUT_OF(int16_t), &off_s));
    TEST_ASSERT_EQUAL_size_t(offsetof(Mixed, i), off_i);
    TEST_ASSERT_EQUAL_size_t(offsetof(Mixed, s), off_s);
    TEST_ASSERT_EQUAL_size_t(alignof(Mixed), l.align);

    TEST_ASSERT_FALSE(tda_layout_ckd_pad(&l));
    TEST_ASSERT_EQUAL_size_t(sizeof(Mixed), l.size);
}

static void test_ckd_extend_with_a_wide_field_skips_to_its_boundary() {
    tda_Layout l = {.size = 16, .align = 8};
    size_t off;
    TEST_ASSERT_FALSE(tda_layout_ckd_extend(&l, TDA_LAYOUT_OF(Wide), &off));
    TEST_ASSERT_EQUAL_size_t(64, off);
    TEST_ASSERT_EQUAL_size_t(128, l.size);
    TEST_ASSERT_EQUAL_size_t(64, l.align);
}

static void test_ckd_extend_and_pad_report_an_overflow() {
    tda_Layout l = {.size = SIZE_MAX - 4, .align = 1};
    size_t off;
    TEST_ASSERT_TRUE(tda_layout_ckd_extend(&l, TDA_LAYOUT_OF(int64_t), &off));

    tda_Layout m = {.size = SIZE_MAX - 2, .align = 8};
    TEST_ASSERT_TRUE(tda_layout_ckd_pad(&m));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_layout_of_takes_sizeof_and_alignof);

    RUN_TEST(test_is_align_takes_powers_of_two_only);
    RUN_TEST(test_is_type_wants_a_size_that_is_a_multiple_of_the_alignment);
    RUN_TEST(test_eq_compares_size_and_alignment);

    RUN_TEST(test_ckd_array_repeats_the_elem_and_keeps_its_alignment);
    RUN_TEST(test_ckd_array_reports_an_overflow);
    RUN_TEST(test_ckd_extend_lays_fields_out_as_a_struct_does);
    RUN_TEST(test_ckd_extend_with_a_wide_field_skips_to_its_boundary);
    RUN_TEST(test_ckd_extend_and_pad_report_an_overflow);

    return UNITY_END();
}
