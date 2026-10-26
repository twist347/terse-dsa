#pragma once

#include "tda/core/status.h"

#include <unity.h>

#include <stdio.h>
#include <string.h>

/* ========== macros ========== */

#define TDA_TEST_STATUS(want, expr) \
    TDA_TEST_STATUS_((want), (expr), #expr)

#define TDA_TEST_OK(expr) \
    TDA_TEST_STATUS_(TDA_STATUS_OK, (expr), #expr)

// fills 'obj' with a byte no constructor writes, so a failed call that resets its 'out'
// to '= {}' is told apart from one that leaves it alone
#define TDA_TEST_POISON(obj) \
    memset(&(obj), 0xA5, sizeof(obj))

#define TDA_TEST_ASSERT_POISONED(obj) \
    TEST_ASSERT_EACH_EQUAL_HEX8(0xA5, &(obj), sizeof(obj))

/* ========== internals ========== */

// 'text' is stringified by the caller, so the message shows what the test
// wrote, not what the preprocessor made of it
#define TDA_TEST_STATUS_(want, expr, text)                                          \
    do {                                                                            \
        const tda_Status tda_test_got_ = (expr);                                    \
        if (tda_test_got_ != (want)) {                                              \
            TEST_FAIL_MESSAGE(tda_test_status_msg_((text), (want), tda_test_got_)); \
        }                                                                           \
    } while (0)

[[nodiscard]]
static inline const char *tda_test_status_msg_(const char *text, tda_Status want, tda_Status got) {
    static char buf[256];
    snprintf(buf, sizeof(buf), "%s: expected %s, got %s", text, tda_status_to_str(want), tda_status_to_str(got));

    return buf;
}
