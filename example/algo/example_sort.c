// for @snippet

#include "tda/algo/sort.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main() {
    /// [sort]
    const tda_SpanMut s = TDA_SPAN_OF_MUT(int32_t, 5, 3, 1, 4, 2);

    // the default: in place, no allocation, not stable
    tda_span_sort(s, tda_cmp_i32);
    tda_span_mut_print(s, tda_fprint_i32); // [1, 2, 3, 4, 5]

    // stability needs a buffer, so the stable sort takes an allocator and can fail
    const tda_SpanMut t = TDA_SPAN_OF_MUT(int32_t, 9, 7, 8);
    if (TDA_STATUS_IS_ERR(tda_span_sort_stable(t, tda_cmp_i32, tda_al_default()))) {
        return 1;
    }
    tda_span_mut_print(t, tda_fprint_i32); // [7, 8, 9]
    /// [sort]

    /// [partial]
    const tda_SpanMut u = TDA_SPAN_OF_MUT(int32_t, 5, 3, 8, 1, 9, 4, 2, 7, 6);

    // only the three smallest are asked for, and only they come out in order
    tda_span_partial_sort(u, 3, tda_cmp_i32);
    tda_span_mut_print(u, tda_fprint_i32); // [1, 2, 3, 4, 5, 9, 7, 6, 8]

    // checks only: the prefix is sorted, the whole span is not
    printf(
        "%s, in order up to %zu\n",
        tda_span_is_sorted(tda_span_mut_to_span(u), tda_cmp_i32) ? "sorted" : "not sorted",
        tda_span_is_sorted_until(tda_span_mut_to_span(u), tda_cmp_i32)
    ); // not sorted, in order up to 6
    /// [partial]

    return 0;
}
