// for @snippet

#include "tda/algo/sort.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <inttypes.h>
#include <stdio.h>

int main() {
    /// [build]
    // a view borrows: the elems live in 'nums', the span only says where and how many
    int32_t nums[] = {5, 3, 1, 4, 2};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, nums, 5);

    // the fields are public: a view owns nothing, so it has no invariant to protect
    printf("%zu elems of %zu bytes, %zu in all\n", s.len, s.elem.size,
           tda_span_bytes(tda_span_mut_to_span(s))); // 5 elems of 4 bytes, 20 in all
    /// [build]

    /// [access]
    TDA_SPAN_SET(int32_t, s, 0, 9);
    tda_span_swap_elems(s, 0, 4);
    printf("%" PRId32 "\n", *TDA_SPAN_GET_MUT_AS(int32_t, s, 0)); // 2

    // writing through the view writes the source: this is the same elem
    printf("%" PRId32 "\n", nums[0]); // 2

    // walking a view needs the elem type named: a span carries sizes, not types
    int32_t total = 0;
    TDA_SPAN_FOR_EACH_AS (int32_t, elem, tda_span_mut_to_span(s)) {
        total += *elem;
    }
    printf("%" PRId32 "\n", total); // 19
    /// [access]

    /// [sub]
    // a subspan views the same memory, offset and shortened; nothing is copied
    const tda_SpanMut tail = tda_span_sub_mut(s, 1, 4);
    tda_span_mut_print(tail, tda_fprint_i32); // [3, 1, 4, 9]
    /// [sub]

    /// [bridge]
    // ds hands out a view and algo works in place through it
    tda_span_sort(s, tda_cmp_i32, nullptr);
    tda_span_print(tda_span_mut_to_span(s), tda_fprint_i32); // [1, 2, 3, 4, 9]
    /// [bridge]

    return 0;
}
