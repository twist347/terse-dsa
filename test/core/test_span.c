#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/util.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>

void setUp() {
}

void tearDown() {
}

// a printer writes to a stream, so a case reads one back through tmpfile, as
// test/core/test_print.c does
static void assert_prints(const char *expected, tda_Span s) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_span_fprint(s, stream, tda_fprint_i32);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof buf - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

// the mut printer is its own symbol, so it needs its own capture
static void assert_mut_prints(const char *expected, tda_SpanMut s) {
    FILE *stream = tmpfile();
    TEST_ASSERT_NOT_NULL(stream);

    tda_span_mut_fprint(s, stream, tda_fprint_i32);
    rewind(stream);

    char buf[128];
    const size_t n = fread(buf, 1, sizeof buf - 1, stream);
    buf[n] = '\0';
    fclose(stream);

    TEST_ASSERT_EQUAL_STRING(expected, buf);
}

/* ========== construction ========== */

/* ========== walk ========== */

static void test_for_each_binds_every_elem_in_order() {
    constexpr int32_t buf[4] = {10, 20, 30, 40};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 4);

    int32_t seen[4];
    size_t n = 0;
    TDA_SPAN_FOR_EACH_AS(int32_t, elem, s) {
        seen[n++] = *elem;
    }

    TEST_ASSERT_EQUAL_size_t(4, n);
    TEST_ASSERT_EQUAL_INT32_ARRAY(buf, seen, 4);
}

// the empty view carries a null data pointer, and the walk must not touch it
static void test_for_each_over_an_empty_view_runs_no_body() {
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, nullptr, 0);

    size_t n = 0;
    TDA_SPAN_FOR_EACH_AS(int32_t, elem, s) {
        TDA_UNUSED(elem);
        ++n;
    }

    TEST_ASSERT_EQUAL_size_t(0, n);
}

// the view is taken once: a walk over tda_span_sub of a call would otherwise redo it
static void test_for_each_evaluates_the_view_once() {
    constexpr int32_t buf[4] = {1, 2, 3, 4};

    int32_t total = 0;
    TDA_SPAN_FOR_EACH_AS(int32_t, elem, tda_span_sub(TDA_SPAN_FROM_DATA(int32_t, buf, 4), 1, 3)) {
        total += *elem;
    }

    TEST_ASSERT_EQUAL_INT32(9, total); // 2 + 3 + 4, and the sub was taken once
}

static void test_for_each_mut_writes_through_the_view() {
    int32_t buf[3] = {1, 2, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3);

    TDA_SPAN_FOR_EACH_MUT_AS(int32_t, elem, s) {
        *elem *= 10;
    }

    TEST_ASSERT_EQUAL_INT32_ARRAY(((int32_t[]){10, 20, 30}), buf, 3);
}

static void test_new_keeps_fields() {
    constexpr int32_t buf[3] = {10, 20, 30};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 3);

    TEST_ASSERT_EQUAL_PTR(buf, s.data);
    TEST_ASSERT_EQUAL_size_t(3, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem_size);
}

// a null view is legal only while empty — elem_size stays meaningful
static void test_new_empty_over_null() {
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, nullptr, 0);

    TEST_ASSERT_NULL(s.data);
    TEST_ASSERT_EQUAL_size_t(0, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem_size);
}

static void test_mut_to_span_preserves_view() {
    const tda_SpanMut m = TDA_SPAN_OF_MUT(int32_t, 1, 2);
    const tda_Span s = tda_span_mut_to_span(m);

    TEST_ASSERT_EQUAL_PTR(m.data, s.data);
    TEST_ASSERT_EQUAL_size_t(m.len, s.len);
    TEST_ASSERT_EQUAL_size_t(m.elem_size, s.elem_size);
}

// mut_to_span must hand back a view of the same memory, not a copy
static void test_mut_to_span_shares_the_memory() {
    int32_t buf[3] = {1, 2, 3};
    const tda_SpanMut m = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3);
    const tda_Span s = tda_span_mut_to_span(m);

    TDA_SPAN_SET(int32_t, m, 1, 99);

    TEST_ASSERT_EQUAL_INT32(99, *TDA_SPAN_GET_AS(int32_t, s, 1));
}

/* ========== of ========== */

static void test_of_views_the_literal() {
    const tda_Span s = TDA_SPAN_OF(int32_t, 3, 1, 2);

    TEST_ASSERT_EQUAL_size_t(3, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), s.elem_size);
    TEST_ASSERT_EQUAL_INT32(3, *TDA_SPAN_GET_AS(int32_t, s, 0));
    TEST_ASSERT_EQUAL_INT32(1, *TDA_SPAN_GET_AS(int32_t, s, 1));
    TEST_ASSERT_EQUAL_INT32(2, *TDA_SPAN_GET_AS(int32_t, s, 2));
}

static void test_of_derives_len_from_the_list() {
    TEST_ASSERT_EQUAL_size_t(1, TDA_SPAN_OF(int32_t, 42).len);
    TEST_ASSERT_EQUAL_size_t(5, TDA_SPAN_OF(int32_t, 1, 2, 3, 4, 5).len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int64_t), TDA_SPAN_OF(int64_t, 1, 2).elem_size);
}

// a brace-enclosed elem survives the macro: the commas split the args,
// __VA_ARGS__ pastes them back
static void test_of_carries_struct_elems() {
    const tda_Span s = TDA_SPAN_OF(Pair, {1, 2}, {3, 4});

    TEST_ASSERT_EQUAL_size_t(2, s.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), s.elem_size);
    TEST_ASSERT_EQUAL_INT64(1, TDA_SPAN_GET_AS(Pair, s, 0)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_SPAN_GET_AS(Pair, s, 0)->b);
    TEST_ASSERT_EQUAL_INT64(3, TDA_SPAN_GET_AS(Pair, s, 1)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_SPAN_GET_AS(Pair, s, 1)->b);
}

// the literal behind a mut view is not const, so it takes writes
static void test_of_mut_is_writable() {
    const tda_SpanMut s = TDA_SPAN_OF_MUT(int32_t, 10, 20, 30);

    TDA_SPAN_SET(int32_t, s, 0, 99);
    *TDA_SPAN_GET_MUT_AS(int32_t, s, 2) = 77;
    tda_span_swap_elems(s, 0, 2);

    const tda_Span v = tda_span_mut_to_span(s);
    TEST_ASSERT_EQUAL_INT32(77, *TDA_SPAN_GET_AS(int32_t, v, 0));
    TEST_ASSERT_EQUAL_INT32(20, *TDA_SPAN_GET_AS(int32_t, v, 1));
    TEST_ASSERT_EQUAL_INT32(99, *TDA_SPAN_GET_AS(int32_t, v, 2));
}

static void test_of_mut_carries_struct_elems() {
    const tda_SpanMut s = TDA_SPAN_OF_MUT(Pair, {1, 2}, {3, 4});

    tda_span_swap_elems(s, 0, 1);

    TEST_ASSERT_EQUAL_INT64(3, TDA_SPAN_GET_MUT_AS(Pair, s, 0)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_SPAN_GET_MUT_AS(Pair, s, 0)->b);
    TEST_ASSERT_EQUAL_INT64(1, TDA_SPAN_GET_MUT_AS(Pair, s, 1)->a);
    TEST_ASSERT_EQUAL_INT64(2, TDA_SPAN_GET_MUT_AS(Pair, s, 1)->b);
}

/* ========== info ========== */

static void test_bytes() {
    constexpr int32_t buf[4] = {0, 0, 0, 0};

    TEST_ASSERT_EQUAL_size_t(4 * sizeof(int32_t), tda_span_bytes(TDA_SPAN_FROM_DATA(int32_t, buf, 4)));
    TEST_ASSERT_EQUAL_size_t(0, tda_span_bytes(TDA_SPAN_FROM_DATA(int32_t, buf, 0)));
}

static void test_bytes_of_null_view_is_zero() {
    TEST_ASSERT_EQUAL_size_t(0, tda_span_bytes(TDA_SPAN_FROM_DATA(int32_t, nullptr, 0)));
}

// elem_size, not the elem count, drives the total
static void test_bytes_tracks_elem_size() {
    TEST_ASSERT_EQUAL_size_t(2 * sizeof(Pair), tda_span_bytes(TDA_SPAN_OF(Pair, {1, 2}, {3, 4})));
}

/* ========== access ========== */

static void test_get_reads_through() {
    const tda_Span s = TDA_SPAN_OF(int32_t, 10, 20, 30);

    TEST_ASSERT_EQUAL_INT32(10, *TDA_SPAN_GET_AS(int32_t, s, 0));
    TEST_ASSERT_EQUAL_INT32(30, *TDA_SPAN_GET_AS(int32_t, s, 2));
}

static void test_set_and_get_mut_write_to_the_source() {
    int32_t buf[3] = {10, 20, 30};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3);

    TDA_SPAN_SET(int32_t, s, 1, 99);
    *TDA_SPAN_GET_MUT_AS(int32_t, s, 2) = 77;

    // the span borrows buf, so the writes must be visible there
    constexpr int32_t expected[3] = {10, 99, 77};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 3);
}

/* ========== subspan ========== */

static void test_sub_offsets_and_shortens() {
    constexpr int32_t buf[5] = {0, 1, 2, 3, 4};
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, buf, 5);

    const tda_Span mid = tda_span_sub(s, 1, 3);
    TEST_ASSERT_EQUAL_PTR(&buf[1], mid.data);
    TEST_ASSERT_EQUAL_size_t(3, mid.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), mid.elem_size);
    TEST_ASSERT_EQUAL_INT32(1, *TDA_SPAN_GET_AS(int32_t, mid, 0));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_SPAN_GET_AS(int32_t, mid, 2));

    // subspans compose: indices are relative to the span they are taken from
    const tda_Span inner = tda_span_sub(mid, 1, 1);
    TEST_ASSERT_EQUAL_size_t(1, inner.len);
    TEST_ASSERT_EQUAL_INT32(2, *TDA_SPAN_GET_AS(int32_t, inner, 0));
}

// the two bounds sub allows: the whole span, and the exact tail
static void test_sub_takes_the_whole_span_and_the_exact_tail() {
    const tda_Span s = TDA_SPAN_OF(int32_t, 0, 1, 2, 3);

    const tda_Span all = tda_span_sub(s, 0, 4);
    TEST_ASSERT_EQUAL_PTR(s.data, all.data);
    TEST_ASSERT_EQUAL_size_t(4, all.len);

    // start > 0 and start + count == len
    const tda_Span tail = tda_span_sub(s, 2, 2);
    TEST_ASSERT_EQUAL_size_t(2, tail.len);
    TEST_ASSERT_EQUAL_INT32(2, *TDA_SPAN_GET_AS(int32_t, tail, 0));
    TEST_ASSERT_EQUAL_INT32(3, *TDA_SPAN_GET_AS(int32_t, tail, 1));
}

// start == len is legal and yields an empty view, not an error
static void test_sub_at_end_is_empty() {
    const tda_Span s = TDA_SPAN_OF(int32_t, 0, 1, 2);

    const tda_Span tail = tda_span_sub(s, 3, 0);
    TEST_ASSERT_EQUAL_size_t(0, tail.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), tail.elem_size);
}

// the offset is start * elem_size, so a wide elem must land on its boundary
static void test_sub_offsets_by_elem_size() {
    const tda_Span s = TDA_SPAN_OF(Pair, {1, 2}, {3, 4}, {5, 6});

    const tda_Span tail = tda_span_sub(s, 1, 2);
    TEST_ASSERT_EQUAL_size_t(2, tail.len);
    TEST_ASSERT_EQUAL_INT64(3, TDA_SPAN_GET_AS(Pair, tail, 0)->a);
    TEST_ASSERT_EQUAL_INT64(4, TDA_SPAN_GET_AS(Pair, tail, 0)->b);
    TEST_ASSERT_EQUAL_INT64(5, TDA_SPAN_GET_AS(Pair, tail, 1)->a);
    TEST_ASSERT_EQUAL_INT64(6, TDA_SPAN_GET_AS(Pair, tail, 1)->b);
}

// a null view must survive subspanning without forming a null + offset pointer
static void test_sub_of_null_view_stays_null() {
    const tda_Span s = TDA_SPAN_FROM_DATA(int32_t, nullptr, 0);

    const tda_Span sub = tda_span_sub(s, 0, 0);
    TEST_ASSERT_NULL(sub.data);
    TEST_ASSERT_EQUAL_size_t(0, sub.len);
}

// the mirror of the case above, for the mut branch
static void test_sub_mut_of_null_view_stays_null() {
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, nullptr, 0);

    const tda_SpanMut sub = tda_span_sub_mut(s, 0, 0);
    TEST_ASSERT_NULL(sub.data);
    TEST_ASSERT_EQUAL_size_t(0, sub.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), sub.elem_size);
}

static void test_sub_mut_writes_reach_the_source() {
    int32_t buf[4] = {0, 1, 2, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);

    const tda_SpanMut tail = tda_span_sub_mut(s, 2, 2);
    TDA_SPAN_SET(int32_t, tail, 0, 88);

    constexpr int32_t expected[4] = {0, 1, 88, 3};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 4);
}

/* ========== mods ========== */

static void test_swap_elems() {
    int32_t buf[4] = {0, 1, 2, 3};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);

    tda_span_swap_elems(s, 0, 3);

    constexpr int32_t expected[4] = {3, 1, 2, 0};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 4);
}

static void test_swap_elems_same_index_is_noop() {
    int32_t buf[3] = {0, 1, 2};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3);

    tda_span_swap_elems(s, 1, 1);

    constexpr int32_t expected[3] = {0, 1, 2};
    TEST_ASSERT_EQUAL_INT32_ARRAY(expected, buf, 3);
}

// element size drives the copy, so a type wider than a word must swap whole
static void test_swap_elems_moves_whole_element() {
    Pair buf[2] = {{1, 2}, {3, 4}};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(Pair, buf, 2);

    tda_span_swap_elems(s, 0, 1);

    TEST_ASSERT_EQUAL_INT64(3, buf[0].a);
    TEST_ASSERT_EQUAL_INT64(4, buf[0].b);
    TEST_ASSERT_EQUAL_INT64(1, buf[1].a);
    TEST_ASSERT_EQUAL_INT64(2, buf[1].b);
}

/* ========== print ========== */

static void test_fprint_writes_the_elems() {
    constexpr int32_t buf[3] = {1, 2, 3};

    assert_prints("[1, 2, 3]\n", TDA_SPAN_FROM_DATA(int32_t, buf, 3));
}

static void test_fprint_of_a_single_elem_has_no_separator() {
    constexpr int32_t buf[1] = {7};

    assert_prints("[7]\n", TDA_SPAN_FROM_DATA(int32_t, buf, 1));
}

static void test_fprint_of_an_empty_span() {
    constexpr int32_t buf[1] = {7};

    assert_prints("[]\n", TDA_SPAN_FROM_DATA(int32_t, buf, 0));
    assert_prints("[]\n", TDA_SPAN_FROM_DATA(int32_t, nullptr, 0));
}

static void test_mut_fprint_writes_what_fprint_writes() {
    int32_t buf[3] = {1, 2, 3};

    assert_mut_prints("[1, 2, 3]\n", TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 3));
    assert_mut_prints("[]\n", TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 0));
}

// the stdout twins take no stream, and C has no portable way to capture one and give it
// back — so a case can only say that they run and reach the same printer
static void test_print_twins_write_to_stdout() {
    int32_t buf[2] = {4, 5};

    tda_span_print(TDA_SPAN_FROM_DATA(int32_t, buf, 2), tda_fprint_i32);
    tda_span_mut_print(TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 2), tda_fprint_i32);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_for_each_binds_every_elem_in_order);
    RUN_TEST(test_for_each_over_an_empty_view_runs_no_body);
    RUN_TEST(test_for_each_evaluates_the_view_once);
    RUN_TEST(test_for_each_mut_writes_through_the_view);

    RUN_TEST(test_new_keeps_fields);
    RUN_TEST(test_new_empty_over_null);
    RUN_TEST(test_mut_to_span_preserves_view);
    RUN_TEST(test_mut_to_span_shares_the_memory);

    RUN_TEST(test_of_views_the_literal);
    RUN_TEST(test_of_derives_len_from_the_list);
    RUN_TEST(test_of_carries_struct_elems);
    RUN_TEST(test_of_mut_is_writable);
    RUN_TEST(test_of_mut_carries_struct_elems);

    RUN_TEST(test_bytes);
    RUN_TEST(test_bytes_of_null_view_is_zero);
    RUN_TEST(test_bytes_tracks_elem_size);

    RUN_TEST(test_get_reads_through);
    RUN_TEST(test_set_and_get_mut_write_to_the_source);

    RUN_TEST(test_sub_offsets_and_shortens);
    RUN_TEST(test_sub_takes_the_whole_span_and_the_exact_tail);
    RUN_TEST(test_sub_at_end_is_empty);
    RUN_TEST(test_sub_offsets_by_elem_size);
    RUN_TEST(test_sub_of_null_view_stays_null);
    RUN_TEST(test_sub_mut_of_null_view_stays_null);
    RUN_TEST(test_sub_mut_writes_reach_the_source);

    RUN_TEST(test_swap_elems);
    RUN_TEST(test_swap_elems_same_index_is_noop);
    RUN_TEST(test_swap_elems_moves_whole_element);

    RUN_TEST(test_fprint_writes_the_elems);
    RUN_TEST(test_fprint_of_a_single_elem_has_no_separator);
    RUN_TEST(test_fprint_of_an_empty_span);
    RUN_TEST(test_mut_fprint_writes_what_fprint_writes);
    RUN_TEST(test_print_twins_write_to_stdout);

    return UNITY_END();
}
