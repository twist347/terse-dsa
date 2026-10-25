// for @snippet

#include "tda/algo/merge.h"
#include "tda/algo/sort.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <stdint.h>

int main() {
    /// [merge]
    const tda_Span a = TDA_SPAN_OF(int32_t, 1, 4, 7), b = TDA_SPAN_OF(int32_t, 2, 3, 8);

    int32_t buf[6];
    const tda_SpanMut dst = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6);

    tda_span_merge(dst, a, b, tda_cmp_i32, nullptr);
    tda_span_mut_print(dst, tda_fprint_i32); // [1, 2, 3, 4, 7, 8]

    // in place: two sorted runs end to end become one; tda_span_is_sorted_until finds
    // where the second run starts
    const tda_SpanMut s = TDA_SPAN_OF_MUT(int32_t, 1, 4, 7, 2, 3, 8);
    const size_t mid = tda_span_is_sorted_until(tda_span_mut_to_span(s), tda_cmp_i32, nullptr);

    // the allocator only speeds this up; with null it runs without a buffer
    tda_span_inplace_merge(s, mid, tda_cmp_i32, nullptr, tda_al_default());
    tda_span_mut_print(s, tda_fprint_i32); // [1, 2, 3, 4, 7, 8]
    /// [merge]

    return 0;
}
