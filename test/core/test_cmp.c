#include "tda/core/cmp.h"
#include "tda/algo/search.h"
#include "tda/algo/sort.h"

#include <unity.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// The by-value form the public API dropped, kept here because these cases want readable
// operands. It is what tda_cmp_<T> used to be: the comparator called with the addresses
// of two values. Instantiated only where a case calls it, so an unused one warns.
#define DEFINE_CMP_FORM(name, T)      \
    static int cmp_##name(T a, T b) { \
        return tda_cmp_##name(&a, &b); \
    }

#define DEFINE_EQ_FORM(name, T)       \
    static bool eq_##name(T a, T b) { \
        return tda_eq_##name(&a, &b);  \
    }

DEFINE_CMP_FORM(i8, int8_t)
DEFINE_CMP_FORM(i16, int16_t)
DEFINE_CMP_FORM(i32, int32_t)
DEFINE_CMP_FORM(i64, int64_t)
DEFINE_CMP_FORM(u8, uint8_t)
DEFINE_CMP_FORM(u16, uint16_t)
DEFINE_CMP_FORM(u32, uint32_t)
DEFINE_CMP_FORM(u64, uint64_t)
DEFINE_CMP_FORM(size, size_t)
DEFINE_CMP_FORM(ptrdiff, ptrdiff_t)
DEFINE_CMP_FORM(char, char)
DEFINE_CMP_FORM(f32, float)
DEFINE_CMP_FORM(f64, double)
DEFINE_CMP_FORM(cstr, const char *)

DEFINE_EQ_FORM(i32, int32_t)
DEFINE_EQ_FORM(u8, uint8_t)
DEFINE_EQ_FORM(size, size_t)
DEFINE_EQ_FORM(char, char)
DEFINE_EQ_FORM(f64, double)
DEFINE_EQ_FORM(cstr, const char *)

#undef DEFINE_CMP_FORM
#undef DEFINE_EQ_FORM

// every comparator must return exactly -1, 0 or 1, not just any int of the right sign
static void expect_order(int less, int equal, int greater) {
    TEST_ASSERT_EQUAL_INT(-1, less);
    TEST_ASSERT_EQUAL_INT(0, equal);
    TEST_ASSERT_EQUAL_INT(1, greater);
}

// a descending comparator is its ascending twin with the operands the other way round
static void expect_desc(tda_Cmp desc, const void *small, const void *big) {
    TEST_ASSERT_EQUAL_INT(1, desc(small, big));
    TEST_ASSERT_EQUAL_INT(-1, desc(big, small));
    TEST_ASSERT_EQUAL_INT(0, desc(small, small));
}

// an equality answers true on the same value and false on a neighbour of it
static void expect_eq(tda_Eq eq, const void *val, const void *same, const void *other) {
    TEST_ASSERT_TRUE(eq(val, same));
    TEST_ASSERT_FALSE(eq(val, other));
}

/* ========== signed ========== */

static void test_cmp_signed_orders_and_normalizes() {
    expect_order(cmp_i8(-1, 0), cmp_i8(7, 7), cmp_i8(0, -1));
    expect_order(cmp_i16(-1, 0), cmp_i16(7, 7), cmp_i16(0, -1));
    expect_order(cmp_i32(-1, 0), cmp_i32(7, 7), cmp_i32(0, -1));
    expect_order(cmp_i64(-1, 0), cmp_i64(7, 7), cmp_i64(0, -1));
}

// the extremes are where a subtracting comparator would overflow and answer backwards
static void test_cmp_signed_survives_the_extremes() {
    expect_order(cmp_i8(INT8_MIN, INT8_MAX), cmp_i8(INT8_MIN, INT8_MIN), cmp_i8(INT8_MAX, INT8_MIN));
    expect_order(cmp_i32(INT32_MIN, INT32_MAX), cmp_i32(INT32_MIN, INT32_MIN), cmp_i32(INT32_MAX, INT32_MIN));
    expect_order(cmp_i64(INT64_MIN, INT64_MAX), cmp_i64(INT64_MIN, INT64_MIN), cmp_i64(INT64_MAX, INT64_MIN));
}

/* ========== unsigned ========== */

static void test_cmp_unsigned_orders_and_normalizes() {
    expect_order(cmp_u8(0, 1), cmp_u8(7, 7), cmp_u8(1, 0));
    expect_order(cmp_u16(0, 1), cmp_u16(7, 7), cmp_u16(1, 0));
    expect_order(cmp_u32(0, 1), cmp_u32(7, 7), cmp_u32(1, 0));
    expect_order(cmp_u64(0, 1), cmp_u64(7, 7), cmp_u64(1, 0));
    expect_order(cmp_size(0, 1), cmp_size(7, 7), cmp_size(1, 0));
}

// unsigned has no negatives to wrap into, but a subtracting comparator still wraps
static void test_cmp_unsigned_survives_the_extremes() {
    expect_order(cmp_u8(0, UINT8_MAX), cmp_u8(0, 0), cmp_u8(UINT8_MAX, 0));
    expect_order(cmp_u64(0, UINT64_MAX), cmp_u64(0, 0), cmp_u64(UINT64_MAX, 0));
    expect_order(cmp_size(0, SIZE_MAX), cmp_size(0, 0), cmp_size(SIZE_MAX, 0));
}

/* ========== ptrdiff ========== */

// size and ptrdiff follow the target's width, so no fixed-width entry can stand in for
// them: on a 32-bit build tda_cmp_u64 would read past a size_t, and tda_cmp_i64 past a
// ptrdiff_t
static void test_cmp_ptrdiff_orders_and_survives_the_extremes() {
    expect_order(cmp_ptrdiff(-1, 0), cmp_ptrdiff(7, 7), cmp_ptrdiff(0, -1));
    expect_order(
        cmp_ptrdiff(PTRDIFF_MIN, PTRDIFF_MAX),
        cmp_ptrdiff(PTRDIFF_MIN, PTRDIFF_MIN),
        cmp_ptrdiff(PTRDIFF_MAX, PTRDIFF_MIN)
    );
}

/* ========== char ========== */

// char is neither signed char nor unsigned char, and which one it behaves like depends on
// the target. So the expected result is not a literal: the comparator must agree with the
// platform's own operators. (char) 200 is negative on x86 and 200 under -funsigned-char;
// tda_cmp_i8 or tda_cmp_u8 in its place is wrong on exactly one of the two.
static void test_cmp_char_follows_the_signedness_of_the_target() {
    constexpr char high = (char) 200;
    constexpr char low = 100;
    constexpr int want = (high > low) - (high < low);

    TEST_ASSERT_EQUAL_INT(want, cmp_char(high, low));
    TEST_ASSERT_EQUAL_INT(-want, cmp_char(low, high));
    TEST_ASSERT_EQUAL_INT(0, cmp_char(high, high));

    TEST_ASSERT_TRUE(eq_char('a', 'a'));
    TEST_ASSERT_FALSE(eq_char('a', 'b'));
}

// the typical use: sorting the letters of a word
static void test_cmp_char_sorts_letters() {
    char buf[5] = {'d', 'a', 'e', 'b', 'c'};

    tda_span_sort(TDA_SPAN_FROM_DATA_MUT(char, buf, 5), tda_cmp_char);

    TEST_ASSERT_EQUAL_CHAR_ARRAY("abcde", buf, 5);
}

/* ========== floats ========== */

static void test_cmp_float_orders_ordinary_values() {
    expect_order(cmp_f32(1.0f, 2.0f), cmp_f32(2.0f, 2.0f), cmp_f32(2.0f, 1.0f));
    expect_order(cmp_f64(1.0, 2.0), cmp_f64(2.0, 2.0), cmp_f64(2.0, 1.0));
}

// IEEE says -0.0 == +0.0, and the ordering must agree, with no tie-break
static void test_cmp_float_treats_the_two_zeroes_as_one() {
    TEST_ASSERT_EQUAL_INT(0, cmp_f32(-0.0f, 0.0f));
    TEST_ASSERT_EQUAL_INT(0, cmp_f64(-0.0, 0.0));
    TEST_ASSERT_TRUE(eq_f64(-0.0, 0.0));
}

// why these are not (a > b) - (a < b): every comparison against NaN is false,
// so the naive form calls NaN equal to everything and the order stops being an order
static void test_cmp_float_sorts_nan_after_every_number() {
    constexpr double nan_val = NAN;

    TEST_ASSERT_EQUAL_INT(1, cmp_f64(nan_val, 0.0));
    TEST_ASSERT_EQUAL_INT(1, cmp_f64(nan_val, INFINITY));
    TEST_ASSERT_EQUAL_INT(-1, cmp_f64(-INFINITY, nan_val));
    TEST_ASSERT_EQUAL_INT(0, cmp_f64(nan_val, nan_val));

    TEST_ASSERT_TRUE(eq_f64(nan_val, nan_val));
    TEST_ASSERT_FALSE(eq_f64(nan_val, 0.0));
}

static void test_cmp_float_orders_the_infinities() {
    expect_order(cmp_f64(-INFINITY, INFINITY), cmp_f64(INFINITY, INFINITY), cmp_f64(INFINITY, -INFINITY));
}

// a span with a NaN in it must still come out ordered, with the NaN at the end
static void test_cmp_float_gives_sort_a_usable_order() {
    double buf[5] = {3.0, NAN, 1.0, -0.0, 2.0};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(double, buf, 5);

    tda_span_sort(s, tda_cmp_f64);

    TEST_ASSERT_TRUE(tda_span_is_sorted(tda_span_mut_to_span(s), tda_cmp_f64));
    TEST_ASSERT_EQUAL_DOUBLE(0.0, buf[0]);
    TEST_ASSERT_EQUAL_DOUBLE(1.0, buf[1]);
    TEST_ASSERT_EQUAL_DOUBLE(2.0, buf[2]);
    TEST_ASSERT_EQUAL_DOUBLE(3.0, buf[3]);
    TEST_ASSERT_TRUE(isnan(buf[4]));
}

/* ========== cstr ========== */

static void test_cmp_cstr_orders_lexicographically() {
    expect_order(cmp_cstr("abc", "abd"), cmp_cstr("abc", "abc"), cmp_cstr("abd", "abc"));
}

// strcmp only promises a sign, and glibc returns values far from ±1
static void test_cmp_cstr_normalizes_to_a_sign() {
    TEST_ASSERT_EQUAL_INT(-1, cmp_cstr("a", "z"));
    TEST_ASSERT_EQUAL_INT(1, cmp_cstr("z", "a"));
}

static void test_cmp_cstr_puts_null_first() {
    TEST_ASSERT_EQUAL_INT(-1, cmp_cstr(nullptr, ""));
    TEST_ASSERT_EQUAL_INT(1, cmp_cstr("", nullptr));
    TEST_ASSERT_EQUAL_INT(0, cmp_cstr(nullptr, nullptr));
    TEST_ASSERT_TRUE(eq_cstr(nullptr, nullptr));
    TEST_ASSERT_FALSE(eq_cstr(nullptr, ""));
}

static void test_cmp_cstr_treats_a_prefix_as_smaller() {
    TEST_ASSERT_EQUAL_INT(-1, cmp_cstr("ab", "abc"));
    TEST_ASSERT_EQUAL_INT(1, cmp_cstr("abc", "ab"));
}

/* ========== equality ========== */

static void test_eq_answers_true_only_on_equal() {
    TEST_ASSERT_TRUE(eq_i32(7, 7));
    TEST_ASSERT_FALSE(eq_i32(7, 8));
    TEST_ASSERT_TRUE(eq_u8(200, 200));
    TEST_ASSERT_FALSE(eq_u8(200, 201));
    TEST_ASSERT_TRUE(eq_size(SIZE_MAX, SIZE_MAX));
    TEST_ASSERT_TRUE(eq_cstr("abc", "abc"));
    TEST_ASSERT_FALSE(eq_cstr("abc", "abd"));
}

// the ready-made equalities the cases above do not spell out, one line per type
static void test_eq_covers_every_ready_made_type() {
    expect_eq(tda_eq_i8, &(int8_t){-8}, &(int8_t){-8}, &(int8_t){-7});
    expect_eq(tda_eq_i16, &(int16_t){-16}, &(int16_t){-16}, &(int16_t){-15});
    expect_eq(tda_eq_i64, &(int64_t){-64}, &(int64_t){-64}, &(int64_t){-63});
    expect_eq(tda_eq_u16, &(uint16_t){16}, &(uint16_t){16}, &(uint16_t){17});
    expect_eq(tda_eq_u32, &(uint32_t){32}, &(uint32_t){32}, &(uint32_t){33});
    expect_eq(tda_eq_u64, &(uint64_t){UINT64_MAX}, &(uint64_t){UINT64_MAX}, &(uint64_t){0});
    expect_eq(tda_eq_ptrdiff, &(ptrdiff_t){-1}, &(ptrdiff_t){-1}, &(ptrdiff_t){1});
    expect_eq(tda_eq_f32, &(float){1.5f}, &(float){1.5f}, &(float){2.5f});
}

/* ========== descending ========== */

// the descending form is the ascending one with its operands the other way round
static void test_descending_inverts_the_ascending_one() {
    constexpr int32_t a = 1;
    constexpr int32_t b = 2;

    TEST_ASSERT_EQUAL_INT(1, tda_cmp_desc_i32(&a, &b));
    TEST_ASSERT_EQUAL_INT(-1, tda_cmp_desc_i32(&b, &a));
    TEST_ASSERT_EQUAL_INT(0, tda_cmp_desc_i32(&a, &a));

    constexpr double x = 1.5;
    constexpr double y = 2.5;
    TEST_ASSERT_EQUAL_INT(1, tda_cmp_desc_f64(&x, &y));
    TEST_ASSERT_EQUAL_INT(-1, tda_cmp_desc_f64(&y, &x));

    // cstr goes through the same shared body despite its pointer-to-pointer operands
    const char *p = "abc";
    const char *q = "abd";
    TEST_ASSERT_EQUAL_INT(1, tda_cmp_desc_cstr(&p, &q));
    TEST_ASSERT_EQUAL_INT(-1, tda_cmp_desc_cstr(&q, &p));
}

// the descending twins the case above does not spell out, one line per type
static void test_descending_covers_every_ready_made_type() {
    expect_desc(tda_cmp_desc_i8, &(int8_t){1}, &(int8_t){2});
    expect_desc(tda_cmp_desc_i16, &(int16_t){1}, &(int16_t){2});
    expect_desc(tda_cmp_desc_i64, &(int64_t){1}, &(int64_t){2});
    expect_desc(tda_cmp_desc_u8, &(uint8_t){1}, &(uint8_t){2});
    expect_desc(tda_cmp_desc_u16, &(uint16_t){1}, &(uint16_t){2});
    expect_desc(tda_cmp_desc_u32, &(uint32_t){1}, &(uint32_t){2});
    expect_desc(tda_cmp_desc_u64, &(uint64_t){1}, &(uint64_t){2});
    expect_desc(tda_cmp_desc_size, &(size_t){1}, &(size_t){2});
    expect_desc(tda_cmp_desc_ptrdiff, &(ptrdiff_t){-1}, &(ptrdiff_t){1});
    expect_desc(tda_cmp_desc_char, &(char){'a'}, &(char){'b'});
    expect_desc(tda_cmp_desc_f32, &(float){1.5f}, &(float){2.5f});
}

/* ========== through the algorithms ========== */

static void test_comparators_drive_sort_both_ways() {
    int32_t buf[5] = {3, 1, 5, 2, 4};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    tda_span_sort(s, tda_cmp_i32);
    constexpr int32_t up[5] = {1, 2, 3, 4, 5};
    TEST_ASSERT_EQUAL_INT32_ARRAY(up, buf, 5);

    tda_span_sort(s, tda_cmp_desc_i32);
    constexpr int32_t down[5] = {5, 4, 3, 2, 1};
    TEST_ASSERT_EQUAL_INT32_ARRAY(down, buf, 5);
}

static void test_cstr_comparator_drives_sort() {
    const char *buf[4] = {"pear", "apple", nullptr, "fig"};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(const char *, buf, 4);

    tda_span_sort(s, tda_cmp_cstr);

    TEST_ASSERT_NULL(buf[0]); // null sorts first
    TEST_ASSERT_EQUAL_STRING("apple", buf[1]);
    TEST_ASSERT_EQUAL_STRING("fig", buf[2]);
    TEST_ASSERT_EQUAL_STRING("pear", buf[3]);
}

static void test_eq_drives_search() {
    constexpr int32_t buf[5] = {10, 20, 30, 20, 10};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 5);

    size_t idx = 0;
    TEST_ASSERT_TRUE(tda_span_find(s, &(int32_t){20}, tda_eq_i32, &idx));
    TEST_ASSERT_EQUAL_size_t(1, idx);
    TEST_ASSERT_EQUAL_size_t(2, tda_span_count(s, &(int32_t){20}, tda_eq_i32));
    TEST_ASSERT_FALSE(tda_span_contains(s, &(int32_t){99}, tda_eq_i32));
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_cmp_signed_orders_and_normalizes);
    RUN_TEST(test_cmp_signed_survives_the_extremes);

    RUN_TEST(test_cmp_unsigned_orders_and_normalizes);
    RUN_TEST(test_cmp_unsigned_survives_the_extremes);

    RUN_TEST(test_cmp_ptrdiff_orders_and_survives_the_extremes);

    RUN_TEST(test_cmp_char_follows_the_signedness_of_the_target);
    RUN_TEST(test_cmp_char_sorts_letters);

    RUN_TEST(test_cmp_float_orders_ordinary_values);
    RUN_TEST(test_cmp_float_treats_the_two_zeroes_as_one);
    RUN_TEST(test_cmp_float_sorts_nan_after_every_number);
    RUN_TEST(test_cmp_float_orders_the_infinities);
    RUN_TEST(test_cmp_float_gives_sort_a_usable_order);

    RUN_TEST(test_cmp_cstr_orders_lexicographically);
    RUN_TEST(test_cmp_cstr_normalizes_to_a_sign);
    RUN_TEST(test_cmp_cstr_puts_null_first);
    RUN_TEST(test_cmp_cstr_treats_a_prefix_as_smaller);

    RUN_TEST(test_eq_answers_true_only_on_equal);
    RUN_TEST(test_eq_covers_every_ready_made_type);

    RUN_TEST(test_descending_inverts_the_ascending_one);
    RUN_TEST(test_descending_covers_every_ready_made_type);

    RUN_TEST(test_comparators_drive_sort_both_ways);
    RUN_TEST(test_cstr_comparator_drives_sort);
    RUN_TEST(test_eq_drives_search);

    return UNITY_END();
}
