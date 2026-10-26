// for @snippet

#include "tda/algo/set.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [ops]
    // both sides sorted by the same cmp; equal elems are counted, not collapsed: 'a'
    // holds two 2s and 'b' holds one
    const tda_Span a = TDA_SPAN_OF(int32_t, 1, 2, 2, 5), b = TDA_SPAN_OF(int32_t, 2, 3, 5);

    // 'dst' must fit the worst case; the return says how much was filled
    int32_t buf[7];
    const tda_SpanMut dst = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 7);

    size_t len = tda_span_set_union(dst, a, b, tda_cmp_i32, nullptr);
    tda_span_mut_print(tda_span_sub_mut(dst, 0, len), tda_fprint_i32, nullptr); // [1, 2, 2, 3, 5]

    len = tda_span_set_intersection(dst, a, b, tda_cmp_i32, nullptr);
    tda_span_mut_print(tda_span_sub_mut(dst, 0, len), tda_fprint_i32, nullptr); // [2, 5]

    len = tda_span_set_difference(dst, a, b, tda_cmp_i32, nullptr);
    tda_span_mut_print(tda_span_sub_mut(dst, 0, len), tda_fprint_i32, nullptr); // [1, 2] — one 2 survives

    printf("%d\n", tda_span_includes(a, TDA_SPAN_OF(int32_t, 2, 5), tda_cmp_i32, nullptr)); // 1
    /// [ops]

    return 0;
}
