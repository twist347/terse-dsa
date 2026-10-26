// for @snippet

#include "tda/algo/heap.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    int32_t buf[6] = {5, 3, 1, 4, 2};
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    // a max-heap: the largest is at 0; the rest has no promised order
    tda_span_make_heap(s, tda_cmp_i32, nullptr);
    printf("largest is %" PRId32 "\n", *TDA_SPAN_GET_MUT_AS(int32_t, s, 0)); // 5

    // a min-heap is the same call with a descending comparator
    /// [build]

    /// [push]
    // push: write the elem at the end, then sift it up over the grown span
    buf[5] = 9;
    const tda_SpanMut grown = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 6);
    tda_span_push_heap(grown, tda_cmp_i32, nullptr);
    printf("largest is %" PRId32 "\n", *TDA_SPAN_GET_MUT_AS(int32_t, grown, 0)); // 9

    // pop: the largest goes to the back and the rest is a heap again
    tda_span_pop_heap(grown, tda_cmp_i32, nullptr);
    const tda_Span rest = tda_span_sub(tda_span_mut_to_span(grown), 0, 5);
    printf(
        "popped %" PRId32 ", %s\n",
        buf[5],
        tda_span_is_heap(rest, tda_cmp_i32, nullptr) ? "the rest is still a heap" : "broken"
    ); // popped 9, the rest is still a heap

    // heapsort is just sort_heap on a heap
    tda_span_sort_heap(s, tda_cmp_i32, nullptr);
    tda_span_mut_print(s, tda_fprint_i32, nullptr); // [1, 2, 3, 4, 5]
    /// [push]

    return 0;
}
