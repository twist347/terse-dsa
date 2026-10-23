// for @snippet

#include "tda/algo/transform.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <stdint.h>

/// [ops]
static void widen_and_square(void *dst, const void *src, void *) {
    const int64_t v = *(const int32_t *) src;
    *(int64_t *) dst = v * v;
}

static void add(void *dst, const void *lhs, const void *rhs, void *) {
    *(int32_t *) dst = *(const int32_t *) lhs + *(const int32_t *) rhs;
}
/// [ops]

int main() {
    /// [map]
    const tda_Span src = TDA_SPAN_OF(int32_t, 1, 2, 3, 4);

    int64_t wide[4];
    const tda_SpanMut dst = TDA_SPAN_FROM_DATA_MUT(int64_t, wide, 4);

    // only the lengths have to match; the elem sizes are 4 and 8
    tda_span_transform(dst, src, widen_and_square, nullptr);
    tda_span_mut_print(dst, tda_fprint_i64); // [1, 4, 9, 16]

    // two sources in step
    const tda_Span other = TDA_SPAN_OF(int32_t, 10, 20, 30, 40);
    const tda_SpanMut sums = TDA_SPAN_OF_MUT(int32_t, 0, 0, 0, 0);

    tda_span_zip_with(sums, src, other, add, nullptr);
    tda_span_mut_print(sums, tda_fprint_i32); // [11, 22, 33, 44]
    /// [map]

    return 0;
}
