#include "tda/core/util.h"
#include "tda/core/span.h"
#include "tda/core/status.h"

#include "support/pair.h"

#include <unity.h>

#include <stdint.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

/* ========== helpers ========== */

#define ANSWER 42

// counts how often an expression is evaluated, so a macro that evaluates its argument
// twice shows up in the count
static int calls;

static int bump() {
    ++calls;

    return calls;
}

// the project marks every fallible op [[nodiscard]]; this stands in for one
[[nodiscard]]
static int nodiscard_bump() {
    return bump();
}

/* ========== stringify ========== */

// the two-level form macro-expands the argument first
static void test_stringify_expands_a_macro_argument() {
    TEST_ASSERT_EQUAL_STRING("42", TDA_STRINGIFY(ANSWER));
}

// the inner form used on its own spells the name unexpanded
static void test_stringify_inner_form_takes_the_name_as_written() {
    TEST_ASSERT_EQUAL_STRING("ANSWER", TDA_STRINGIFY_(ANSWER));
}

// status.c relies on this: an enumerator is not a macro, so expansion is a no-op and the
// name survives. Were it a macro, tda_status_to_str would return its value instead.
static void test_stringify_leaves_an_enumerator_as_its_own_name() {
    TEST_ASSERT_EQUAL_STRING("TDA_STATUS_OK", TDA_STRINGIFY(TDA_STATUS_OK));
}

// spelling is the preprocessor's, not the caller's: runs of whitespace collapse to one
static void test_stringify_normalizes_spacing() {
    TEST_ASSERT_EQUAL_STRING("1 + 2", TDA_STRINGIFY( 1 + 2 ));
}

/* ========== unused ========== */

// TDA_UNUSED discards the value, not the work: the expression still runs, so it is safe
// to wrap a call whose side effect is wanted
static void test_unused_evaluates_its_argument() {
    calls = 0;

    TDA_UNUSED(bump());

    TEST_ASSERT_EQUAL_INT(1, calls);
}

static void test_unused_evaluates_its_argument_exactly_once() {
    calls = 0;

    TDA_UNUSED(bump());
    TDA_UNUSED(bump());

    TEST_ASSERT_EQUAL_INT(2, calls);
}

// the real check is that the file compiles: a bare call would trip -Wunused-result, and
// the tests build with -Wall -Wextra. The counter only proves the call still runs.
static void test_unused_silences_a_nodiscard_result() {
    calls = 0;

    TDA_UNUSED(nodiscard_bump());

    TEST_ASSERT_EQUAL_INT(1, calls);
}

/* ========== swap ========== */

static void test_swap_exchanges_scalars() {
    int32_t a = 1;
    int32_t b = 2;

    TDA_SWAP(a, b);

    TEST_ASSERT_EQUAL_INT32(2, a);
    TEST_ASSERT_EQUAL_INT32(1, b);
}

// an elem wider than a word must be copied whole, not just its first field
static void test_swap_exchanges_structs() {
    Pair a = {.a = 1, .b = 2};
    Pair b = {.a = 30, .b = 40};

    TDA_SWAP(a, b);

    TEST_ASSERT_EQUAL_INT64(30, a.a);
    TEST_ASSERT_EQUAL_INT64(40, a.b);
    TEST_ASSERT_EQUAL_INT64(1, b.a);
    TEST_ASSERT_EQUAL_INT64(2, b.b);
}

static void test_swap_exchanges_pointers() {
    int32_t x = 1;
    int32_t y = 2;
    int32_t *p = &x;
    int32_t *q = &y;

    TDA_SWAP(p, q);

    TEST_ASSERT_EQUAL_PTR(&y, p);
    TEST_ASSERT_EQUAL_PTR(&x, q);
}

// the shape the library actually uses: tda_vec_swap and tda_arr_swap swap whole headers
// through TDA_SWAP(*self, *other)
static void test_swap_exchanges_through_dereferenced_pointers() {
    Pair x = {.a = 1, .b = 2};
    Pair y = {.a = 3, .b = 4};
    Pair *px = &x;
    Pair *py = &y;

    TDA_SWAP(*px, *py);

    TEST_ASSERT_EQUAL_INT64(3, x.a);
    TEST_ASSERT_EQUAL_INT64(1, y.a);
}

// a view is a struct of three fields; swapping must move all of them, not just data
static void test_swap_exchanges_whole_spans() {
    int32_t buf[4] = {1, 2, 3, 4};
    tda_Span a = TDA_SPAN_FROM_DATA(int32_t, buf, 4);
    tda_Span b = tda_span_from_data(nullptr, 0, TDA_LAYOUT_OF(Pair));

    TDA_SWAP(a, b);

    TEST_ASSERT_NULL(a.data);
    TEST_ASSERT_EQUAL_size_t(0, a.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(Pair), a.elem.size);
    TEST_ASSERT_EQUAL_PTR(buf, b.data);
    TEST_ASSERT_EQUAL_size_t(4, b.len);
    TEST_ASSERT_EQUAL_size_t(sizeof(int32_t), b.elem.size);
}

// The macro takes each side's address once and reaches the value only through that
// pointer, so an argument with a side effect is safe, unlike in a plain three-assignment
// macro.
static void test_swap_evaluates_each_side_exactly_once() {
    int32_t v[4] = {10, 20, 30, 40};
    size_t i = 0;
    size_t j = 2;

    TDA_SWAP(v[i++], v[j++]);

    TEST_ASSERT_EQUAL_size_t(1, i);
    TEST_ASSERT_EQUAL_size_t(3, j);
    TEST_ASSERT_EQUAL_INT32(30, v[0]);
    TEST_ASSERT_EQUAL_INT32(20, v[1]);
    TEST_ASSERT_EQUAL_INT32(10, v[2]);
    TEST_ASSERT_EQUAL_INT32(40, v[3]);
}

// self swap is a real call site: tda_vec_swap(v, v) reaches it before the identity check
// would, and a macro that wrote through both pointers in the wrong order would lose x
static void test_swap_with_itself_changes_nothing() {
    int32_t x = 5;

    TDA_SWAP(x, x);

    TEST_ASSERT_EQUAL_INT32(5, x);
}

// without the do/while(0) wrapper this else would belong to nothing
static void test_swap_is_a_single_statement() {
    int32_t a = 1;
    int32_t b = 2;

    if (a < b)
        TDA_SWAP(a, b);
    else
        TEST_FAIL_MESSAGE("took the wrong branch");

    TEST_ASSERT_EQUAL_INT32(2, a);
    TEST_ASSERT_EQUAL_INT32(1, b);
}

// the macro's own temporary is declared after the typeof that reads the argument, so a
// caller variable of the same name is still swapped correctly
static void test_swap_survives_a_caller_variable_named_like_its_temporary() {
    int32_t tda_swap_tmp_ = 1;
    int32_t other = 2;

    TDA_SWAP(tda_swap_tmp_, other);

    TEST_ASSERT_EQUAL_INT32(2, tda_swap_tmp_);
    TEST_ASSERT_EQUAL_INT32(1, other);
}

// twice in a row: the second call must not see anything left by the first
static void test_swap_is_repeatable() {
    int32_t a = 1;
    int32_t b = 2;

    TDA_SWAP(a, b);
    TDA_SWAP(a, b);

    TEST_ASSERT_EQUAL_INT32(1, a);
    TEST_ASSERT_EQUAL_INT32(2, b);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_stringify_expands_a_macro_argument);
    RUN_TEST(test_stringify_inner_form_takes_the_name_as_written);
    RUN_TEST(test_stringify_leaves_an_enumerator_as_its_own_name);
    RUN_TEST(test_stringify_normalizes_spacing);

    RUN_TEST(test_unused_evaluates_its_argument);
    RUN_TEST(test_unused_evaluates_its_argument_exactly_once);
    RUN_TEST(test_unused_silences_a_nodiscard_result);

    RUN_TEST(test_swap_exchanges_scalars);
    RUN_TEST(test_swap_exchanges_structs);
    RUN_TEST(test_swap_exchanges_pointers);
    RUN_TEST(test_swap_exchanges_through_dereferenced_pointers);
    RUN_TEST(test_swap_exchanges_whole_spans);
    RUN_TEST(test_swap_evaluates_each_side_exactly_once);
    RUN_TEST(test_swap_with_itself_changes_nothing);
    RUN_TEST(test_swap_is_a_single_statement);
    RUN_TEST(test_swap_survives_a_caller_variable_named_like_its_temporary);
    RUN_TEST(test_swap_is_repeatable);

    return UNITY_END();
}
