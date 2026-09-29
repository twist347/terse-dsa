#include "tda/core/print.h"

#include <unity.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// A printer writes to a stream, so a case has to read one back. tmpfile is the only
// portable in-memory-ish stream there is — fmemopen is POSIX, and the library builds with
// extensions off.
static void capture(char *buf, size_t cap, tda_FPrint fprint, const void *x) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    fprint(stream, x);
    rewind(stream);

    const size_t n = fread(buf, 1, cap - 1, stream);
    buf[n] = '\0';
    fclose(stream);
}

static void assert_prints(const char *expected, tda_FPrint fprint, const void *x) {
    char buf[64];
    capture(buf, sizeof(buf), fprint, x);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

/* ========== int ========== */

static void test_int_prints_the_value() {
    assert_prints("-8", tda_fprint_i8, &(int8_t){-8});
    assert_prints("-16", tda_fprint_i16, &(int16_t){-16});
    assert_prints("-32", tda_fprint_i32, &(int32_t){-32});
    assert_prints("-64", tda_fprint_i64, &(int64_t){-64});
}

static void test_int_prints_the_extremes() {
    assert_prints("-128", tda_fprint_i8, &(int8_t){INT8_MIN});
    assert_prints("127", tda_fprint_i8, &(int8_t){INT8_MAX});
    assert_prints("-9223372036854775808", tda_fprint_i64, &(int64_t){INT64_MIN});
    assert_prints("9223372036854775807", tda_fprint_i64, &(int64_t){INT64_MAX});
}

/* ========== uint ========== */

static void test_uint_prints_the_value() {
    assert_prints("8", tda_fprint_u8, &(uint8_t){8});
    assert_prints("16", tda_fprint_u16, &(uint16_t){16});
    assert_prints("32", tda_fprint_u32, &(uint32_t){32});
    assert_prints("64", tda_fprint_u64, &(uint64_t){64});
}

static void test_uint_prints_the_extremes() {
    assert_prints("255", tda_fprint_u8, &(uint8_t){UINT8_MAX});
    assert_prints("18446744073709551615", tda_fprint_u64, &(uint64_t){UINT64_MAX});
}

/* ========== target width ========== */

static void test_size_and_ptrdiff_print_the_value() {
    assert_prints("0", tda_fprint_size, &(size_t){0});
    assert_prints("4096", tda_fprint_size, &(size_t){4096});
    assert_prints("-1", tda_fprint_ptrdiff, &(ptrdiff_t){-1});
}

/* ========== float ========== */

static void test_float_prints_through_g() {
    assert_prints("1.5", tda_fprint_f32, &(float){1.5f});
    assert_prints("1.5", tda_fprint_f64, &(double){1.5});
    assert_prints("0", tda_fprint_f64, &(double){0.0});
}

// %g is the documented trade: readable, and not a round trip. The case exists so that
// changing the format has to be a decision rather than an accident.
static void test_float_does_not_round_trip() {
    assert_prints("0.1", tda_fprint_f64, &(double){0.1});
    assert_prints("1.23457e+06", tda_fprint_f64, &(double){1234567.0});
}

static void test_float_prints_the_specials() {
    assert_prints("inf", tda_fprint_f32, &(float){INFINITY});
    assert_prints("-inf", tda_fprint_f32, &(float){-INFINITY});
    assert_prints("inf", tda_fprint_f64, &(double){INFINITY});
    assert_prints("-inf", tda_fprint_f64, &(double){-INFINITY});
}

// both signs on purpose: the C library would print the negative one as "-nan", or
// "-nan(ind)" in the UCRT
static void test_float_prints_nan_without_its_sign() {
    assert_prints("nan", tda_fprint_f32, &(float){NAN});
    assert_prints("nan", tda_fprint_f32, &(float){copysignf(NAN, -1.0f)});
    assert_prints("nan", tda_fprint_f64, &(double){NAN});
    assert_prints("nan", tda_fprint_f64, &(double){copysign(NAN, -1.0)});
}

// -0.0 is a distinct value that tda_cmp_f64 deliberately treats as equal to +0.0; the
// printer is not the comparator and shows what is really there
static void test_float_prints_negative_zero() {
    assert_prints("-0", tda_fprint_f64, &(double){-0.0});
}

/* ========== bool ========== */

// words, not digits: an entry that printed 1 and 0 would be tda_fprint_u8 under another
// name, and there would be no reason for it to exist
static void test_bool_prints_words() {
    assert_prints("true", tda_fprint_bool, &(bool){true});
    assert_prints("false", tda_fprint_bool, &(bool){false});
}

/* ========== char ========== */

static void test_char_prints_printable_as_itself() {
    assert_prints("a", tda_fprint_char, &(char){'a'});
    assert_prints(" ", tda_fprint_char, &(char){' '});
    assert_prints("~", tda_fprint_char, &(char){'~'});
}

static void test_char_escapes_the_unprintable() {
    assert_prints("\\x00", tda_fprint_char, &(char){'\0'});
    assert_prints("\\x0a", tda_fprint_char, &(char){'\n'});
    assert_prints("\\x7f", tda_fprint_char, &(char){0x7f});
}

// char may be signed, and isprint on a negative int is undefined: the printer casts to
// unsigned char first, so a high byte comes out as an escape rather than as chaos
static void test_char_escapes_a_high_byte() {
    assert_prints("\\xff", tda_fprint_char, &(char){(char) 0xff});
}

/* ========== cstr ========== */

static void test_cstr_prints_quoted() {
    assert_prints("\"abc\"", tda_fprint_cstr, &(const char *){"abc"});
    assert_prints("\"\"", tda_fprint_cstr, &(const char *){""});
}

// the quotes are what keep a comma inside a string from reading as an elem boundary once
// a container prints [a, b] around it
static void test_cstr_quotes_keep_a_comma_inside() {
    assert_prints("\"a, b\"", tda_fprint_cstr, &(const char *){"a, b"});
}

// null is a value of its own rather than the empty string, as in tda_eq_cstr and
// tda_hash_cstr, so it prints unquoted and cannot be confused with ""
static void test_cstr_prints_null_unquoted() {
    assert_prints("null", tda_fprint_cstr, &(const char *){nullptr});
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_int_prints_the_value);
    RUN_TEST(test_int_prints_the_extremes);

    RUN_TEST(test_uint_prints_the_value);
    RUN_TEST(test_uint_prints_the_extremes);

    RUN_TEST(test_size_and_ptrdiff_print_the_value);

    RUN_TEST(test_float_prints_through_g);
    RUN_TEST(test_float_does_not_round_trip);
    RUN_TEST(test_float_prints_the_specials);
    RUN_TEST(test_float_prints_nan_without_its_sign);
    RUN_TEST(test_float_prints_negative_zero);

    RUN_TEST(test_bool_prints_words);

    RUN_TEST(test_char_prints_printable_as_itself);
    RUN_TEST(test_char_escapes_the_unprintable);
    RUN_TEST(test_char_escapes_a_high_byte);

    RUN_TEST(test_cstr_prints_quoted);
    RUN_TEST(test_cstr_quotes_keep_a_comma_inside);
    RUN_TEST(test_cstr_prints_null_unquoted);

    return UNITY_END();
}
