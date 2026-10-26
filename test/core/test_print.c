#include "tda/core/print.h"
#include "tda/core/span.h"

#include <unity.h>

#include <math.h>
#include <stddef.h>
#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// A printer writes to a stream, so a case reads one back. tmpfile is the only portable
// choice: fmemopen is POSIX, and the library builds with extensions off.
static void capture(char *buf, size_t cap, tda_FPrint fprint, const void *val) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    TEST_ASSERT_TRUE(fprint(stream, val, nullptr) >= 0);
    rewind(stream);

    const size_t n = fread(buf, 1, cap - 1, stream);
    buf[n] = '\0';
    fclose(stream);
}

static void assert_prints(const char *expected, tda_FPrint fprint, const void *val) {
    char buf[64];
    capture(buf, sizeof(buf), fprint, val);

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

// %g is documented as readable but not round-trip; this case makes any change of format
// a deliberate one
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

// words, not digits: printing 1 and 0 would make it tda_fprint_u8 under another name
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
// unsigned char first, so a high byte comes out as an escape
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

// null is a value of its own, not the empty string, as in tda_eq_cstr and tda_hash_cstr,
// so it prints unquoted and cannot be confused with ""
static void test_cstr_prints_null_unquoted() {
    assert_prints("null", tda_fprint_cstr, &(const char *){nullptr});
}

/* ========== span ========== */

static void capture_span(char *buf, size_t cap, tda_Span s, tda_FPrint fprint, void *ctx, int *out_rc) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    *out_rc = tda_span_fprint(s, stream, fprint, ctx);
    rewind(stream);

    const size_t n = fread(buf, 1, cap - 1, stream);
    buf[n] = '\0';
    fclose(stream);
}

// the brackets and no newline, so the output can sit inside another container's
static void test_span_fprint_writes_no_newline() {
    char buf[64];
    int rc;
    capture_span(buf, sizeof(buf), TDA_SPAN_OF(int32_t, 1, 2), tda_fprint_i32, nullptr, &rc);

    TEST_ASSERT_EQUAL_STRING("[1, 2]", buf);
    TEST_ASSERT_EQUAL_INT(0, rc);
}

static int fprint_scaled(FILE *stream, const void *val, void *ctx) {
    return fprintf(stream, "%d", *(const int32_t *) val * *(const int *) ctx);
}

static void test_span_fprint_hands_the_ctx_to_every_call() {
    char buf[64];
    int rc;
    capture_span(buf, sizeof(buf), TDA_SPAN_OF(int32_t, 1, 2, 3), fprint_scaled, &(int){10}, &rc);

    TEST_ASSERT_EQUAL_STRING("[10, 20, 30]", buf);
}

static int fprint_inner(FILE *stream, const void *val, void *) {
    return tda_span_fprint(*(const tda_Span *) val, stream, tda_fprint_i32, nullptr);
}

static void test_span_fprint_nests() {
    const tda_Span rows = TDA_SPAN_OF(tda_Span, TDA_SPAN_OF(int32_t, 1, 2), TDA_SPAN_OF(int32_t, 3));
    char buf[64];
    int rc;
    capture_span(buf, sizeof(buf), rows, fprint_inner, nullptr, &rc);

    TEST_ASSERT_EQUAL_STRING("[[1, 2], [3]]", buf);
}

// a printer that fails on its second elem
static int fprint_fails_on_two(FILE *stream, const void *val, void *) {
    const int32_t v = *(const int32_t *) val;

    return v == 2 ? -1 : fprintf(stream, "%d", v);
}

// a failed write is reported, and the rest is still written, as a run of fprintf would be
static void test_span_fprint_reports_a_failed_write() {
    char buf[64];
    int rc;
    capture_span(buf, sizeof(buf), TDA_SPAN_OF(int32_t, 1, 2, 3), fprint_fails_on_two, nullptr, &rc);

    TEST_ASSERT_EQUAL_INT(-1, rc);
    TEST_ASSERT_EQUAL_STRING("[1, , 3]", buf);
}

// a stream that takes no write: every printer must say so, the spelled-out specials too
static void test_every_printer_reports_a_failed_write() {
    FILE *ro = fopen(__FILE__, "r");
    TEST_ASSERT_NOT_NULL(ro);

    TEST_ASSERT_LESS_THAN_INT(0, tda_fprint_i32(ro, &(int32_t){1}, nullptr));
    TEST_ASSERT_LESS_THAN_INT(0, tda_fprint_f64(ro, &(double){NAN}, nullptr));
    TEST_ASSERT_LESS_THAN_INT(0, tda_fprint_f64(ro, &(double){-INFINITY}, nullptr));
    TEST_ASSERT_LESS_THAN_INT(0, tda_fprint_bool(ro, &(bool){true}, nullptr));
    TEST_ASSERT_LESS_THAN_INT(0, tda_fprint_char(ro, &(char){'a'}, nullptr));
    TEST_ASSERT_LESS_THAN_INT(0, tda_fprint_char(ro, &(char){'\n'}, nullptr));
    TEST_ASSERT_LESS_THAN_INT(0, tda_fprint_cstr(ro, &(const char *){nullptr}, nullptr));
    TEST_ASSERT_LESS_THAN_INT(0, tda_fprint_cstr(ro, &(const char *){"x"}, nullptr));

    fclose(ro);
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

    RUN_TEST(test_span_fprint_writes_no_newline);
    RUN_TEST(test_span_fprint_hands_the_ctx_to_every_call);
    RUN_TEST(test_span_fprint_nests);
    RUN_TEST(test_span_fprint_reports_a_failed_write);

    RUN_TEST(test_every_printer_reports_a_failed_write);

    return UNITY_END();
}
