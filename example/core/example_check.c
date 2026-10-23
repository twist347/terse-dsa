// for @snippet

#include "tda/alloc/default.h"
#include "tda/core/check.h"
#include "tda/ds/vec.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [check]
// for a precondition of your own whose breach would be undefined in a release build too:
// it stays in every build and aborts with the condition, the file, the line and the
// function
static int32_t mean(const int32_t *vals, size_t len) {
    TDA_CHECK(len > 0); // the division below needs it

    int64_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += vals[i];
    }

    return (int32_t) (sum / (int64_t) len);
}
/// [check]

int main() {
    constexpr int32_t vals[] = {3, 4, 8};
    printf("%" PRId32 "\n", mean(vals, 3)); // 5

    /// [hardened]
    // the library checks its own bounds and emptiness with TDA_EXPECT: an assert in a
    // debug build, nothing under NDEBUG, and a TDA_CHECK in a release build configured
    // with -DTDA_HARDENED=ON. An index past the end of a vec of three:
    //
    //   tda_vec_get(&v, 3);
    //
    //   debug:     Assertion `idx < self->len_' failed.
    //   release:   reads past the end, undefined
    //   hardened:  vec.h:<line>: tda_vec_get: check failed: idx < self->len_
    tda_Vec v;
    if (TDA_STATUS_IS_ERR(TDA_VEC_OF(int32_t, tda_al_default(), &v, 1, 2, 3))) {
        return 1;
    }
    printf("%" PRId32 "\n", *TDA_VEC_GET_AS(int32_t, &v, 2)); // 3, the last one in bounds
    tda_vec_deinit(&v);
    /// [hardened]

    return 0;
}
