#include "tda/core/status.h"

#include <unity.h>

#include <stdint.h>
#include <string.h>

void setUp() {
}

void tearDown() {
}

// the enum has a fixed underlying type, so a value no enumerator names is an ordinary
// int32_t and casting one back is defined — that is the only reason the fallback below
// is reachable from valid code at all
static_assert(sizeof(tda_Status) == sizeof(int32_t));

// zero is success: it is what a calloc'd struct holds and what TDA_STATUS_IS_OK reads
static_assert(TDA_STATUS_OK == 0);

// spelled out by hand on purpose. deriving the names from the enum the way status.c does
// (TDA_STRINGIFY over the same list) would make the test agree with any mutation of it —
// the point is to say each name twice, in two places
static const struct {
    tda_Status st;
    const char *name;
} NAMED[] = {
    {TDA_STATUS_OK, "TDA_STATUS_OK"},
    {TDA_STATUS_ERR_NO_MEM, "TDA_STATUS_ERR_NO_MEM"}
};

static constexpr size_t NAMED_LEN = sizeof(NAMED) / sizeof(NAMED[0]);

// values no enumerator has; the last two are the ends of the underlying type
static constexpr tda_Status UNKNOWN[] = {
    (tda_Status) 42,
    (tda_Status) -1,
    (tda_Status) INT32_MAX,
    (tda_Status) INT32_MIN,
};

static constexpr size_t UNKNOWN_LEN = sizeof(UNKNOWN) / sizeof(UNKNOWN[0]);

/* ========== to str ========== */

static void test_status_to_str_spells_every_status() {
    for (size_t i = 0; i < NAMED_LEN; ++i) {
        TEST_ASSERT_EQUAL_STRING(NAMED[i].name, tda_status_to_str(NAMED[i].st));
    }
}

// what the function is for: telling two statuses apart in a message
static void test_status_to_str_gives_each_status_its_own_name() {
    for (size_t i = 0; i < NAMED_LEN; ++i) {
        for (size_t j = i + 1; j < NAMED_LEN; ++j) {
            const char *a = tda_status_to_str(NAMED[i].st);
            const char *b = tda_status_to_str(NAMED[j].st);

            TEST_ASSERT_NOT_EQUAL_INT(0, strcmp(a, b));
        }
    }
}

// a name has to still read the same after the next call, or two of them cannot share one
// printf. pointer identity is deliberately not asserted: any storage outliving the call
// satisfies the contract, a shared buffer does not
static void test_status_to_str_does_not_hand_out_a_shared_buffer() {
    const char *first = tda_status_to_str(TDA_STATUS_OK);
    const char *second = tda_status_to_str(TDA_STATUS_ERR_NO_MEM);

    TEST_ASSERT_EQUAL_STRING("TDA_STATUS_OK", first);
    TEST_ASSERT_EQUAL_STRING("TDA_STATUS_ERR_NO_MEM", second);
}

/* ========== values outside the enum ========== */

static void test_status_to_str_falls_back_outside_the_enum() {
    for (size_t i = 0; i < UNKNOWN_LEN; ++i) {
        TEST_ASSERT_EQUAL_STRING("UNKNOWN_TDA_STATUS", tda_status_to_str(UNKNOWN[i]));
    }
}

// a guard, not a case about the value: add a status after TDA_STATUS_UNSUPPORTED and this
// fails, which is what sends you back to NAMED. -Wswitch does the same job for status.c,
// nothing does it for the table above
static void test_status_to_str_notices_a_status_added_to_the_end() {
    constexpr tda_Status past_the_end = (tda_Status) (TDA_STATUS_ERR_NO_MEM + 1);

    TEST_ASSERT_EQUAL_STRING("UNKNOWN_TDA_STATUS", tda_status_to_str(past_the_end));
}

/* ========== macros ========== */

static void test_status_macros_split_ok_from_everything_else() {
    for (size_t i = 0; i < NAMED_LEN; ++i) {
        const tda_Status st = NAMED[i].st;
        const bool ok = st == TDA_STATUS_OK;

        TEST_ASSERT_EQUAL_INT(ok, TDA_STATUS_IS_OK(st));
        TEST_ASSERT_EQUAL_INT(!ok, TDA_STATUS_IS_ERR(st));
    }
}

// nothing in the tree produces one, but a caller holding a status from an older build can
static void test_status_macros_read_an_unknown_value_as_an_error() {
    for (size_t i = 0; i < UNKNOWN_LEN; ++i) {
        TEST_ASSERT_FALSE(TDA_STATUS_IS_OK(UNKNOWN[i]));
        TEST_ASSERT_TRUE(TDA_STATUS_IS_ERR(UNKNOWN[i]));
    }
}

static size_t arg_evals = 0;

static tda_Status counted(tda_Status st) {
    ++arg_evals;
    return st;
}

// if (TDA_STATUS_IS_ERR(tda_vec_push(&v, &x))) is the form these macros exist for, and
// evaluating the argument twice there would push twice. every call site in the tree today
// passes a variable, so this case is the only thing holding that line
static void test_status_macros_evaluate_their_argument_once() {
    arg_evals = 0;
    TEST_ASSERT_TRUE(TDA_STATUS_IS_OK(counted(TDA_STATUS_OK)));
    TEST_ASSERT_EQUAL_size_t(1, arg_evals);

    arg_evals = 0;
    TEST_ASSERT_TRUE(TDA_STATUS_IS_ERR(counted(TDA_STATUS_ERR_NO_MEM)));
    TEST_ASSERT_EQUAL_size_t(1, arg_evals);
}

int main() {
    UNITY_BEGIN();

    RUN_TEST(test_status_to_str_spells_every_status);
    RUN_TEST(test_status_to_str_gives_each_status_its_own_name);
    RUN_TEST(test_status_to_str_does_not_hand_out_a_shared_buffer);

    RUN_TEST(test_status_to_str_falls_back_outside_the_enum);
    RUN_TEST(test_status_to_str_notices_a_status_added_to_the_end);

    RUN_TEST(test_status_macros_split_ok_from_everything_else);
    RUN_TEST(test_status_macros_read_an_unknown_value_as_an_error);
    RUN_TEST(test_status_macros_evaluate_their_argument_once);

    return UNITY_END();
}
