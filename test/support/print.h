#pragma once

#include "tda/core/print.h"

#include <unity.h>

#include <inttypes.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* ========== a printer that fails on demand ==========
 *
 * What a container's fprint and print owe a printer: its ctx on every call, every elem
 * even after a failure, and the first failure back to the caller.
 */

typedef struct {
    size_t fail_from; ///< the first call to fail, counting from 1; 0 never fails
    size_t calls;     ///< how many calls came
} tda_TestPrint;

/// an int32_t printer that asserts it got its ctx, and from the 'fail_from'th call on
/// writes nothing and returns minus its call number, so the caller can tell which
/// failure it passed on
static inline int tda_test_fprint_i32_failing(FILE *stream, const void *val, void *ctx) {
    TEST_ASSERT_NOT_NULL(ctx);
    tda_TestPrint *self = ctx;
    ++self->calls;

    if (self->fail_from != 0 && self->calls >= self->fail_from) {
        return -(int) self->calls;
    }

    return fprintf(stream, "%" PRId32, *(const int32_t *) val);
}

/// a stream every write to fails: one opened for reading
static inline FILE *tda_test_unwritable_stream() {
    FILE *stream = fopen(__FILE__, "r");
    TEST_ASSERT_NOT_NULL(stream);

    return stream;
}
