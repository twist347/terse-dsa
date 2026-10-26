// for @snippet

#include "tda/algo/fold.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [ops]
static void add_into(void *acc, const void *elem, void *) {
    *(int64_t *) acc += *(const int32_t *) elem;
}

static void add(void *dst, const void *lhs, const void *rhs, void *) {
    *(int32_t *) dst = *(const int32_t *) lhs + *(const int32_t *) rhs;
}

static void sub(void *dst, const void *lhs, const void *rhs, void *) {
    *(int32_t *) dst = *(const int32_t *) lhs - *(const int32_t *) rhs;
}
/// [ops]

int main() {
    /// [fold]
    const tda_Span s = TDA_SPAN_OF(int32_t, 1, 2, 3, 4);

    // the initial value is the caller's and an empty span returns it unchanged, so it
    // should be the identity of the op
    int64_t sum = 0;
    tda_span_fold(s, &sum, add_into, nullptr);
    printf("%" PRId64 "\n", sum); // 10

    // a scan keeps every partial result a fold discards
    int32_t buf[4];
    const tda_SpanMut running = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 4);

    tda_span_partial_sum(running, s, add, nullptr);
    tda_span_mut_print(running, tda_fprint_i32, nullptr); // [1, 3, 6, 10]

    // and undoes it, given the inverse op
    int32_t back[4];
    const tda_SpanMut steps = TDA_SPAN_FROM_DATA_MUT(int32_t, back, 4);

    tda_span_adjacent_difference(steps, tda_span_mut_to_span(running), sub, nullptr);
    tda_span_mut_print(steps, tda_fprint_i32, nullptr); // [1, 2, 3, 4] — back to where it started
    /// [fold]

    return 0;
}
