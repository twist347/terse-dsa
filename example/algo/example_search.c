// for @snippet

#include "tda/algo/search.h"
#include "tda/core/cmp.h"
#include "tda/core/span.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [pred]
static bool greater_than(const void *elem, void *ctx) {
    return *(const int32_t *) elem > *(const int32_t *) ctx;
}

/// [pred]

int main() {
    /// [find]
    // a find is a linear scan, so it asks nothing of the order
    const tda_Span nums = TDA_SPAN_OF(int32_t, 5, 3, 1, 4, 1, 2);

    size_t idx;
    if (tda_span_find(nums, &(int32_t){1}, tda_eq_i32, &idx)) {
        printf("first 1 at %zu\n", idx); // first 1 at 2
    }

    if (tda_span_find_last(nums, &(int32_t){1}, tda_eq_i32, &idx)) {
        printf("last 1 at %zu\n", idx); // last 1 at 4
    }

    int32_t bound = 3;
    if (tda_span_find_if(nums, greater_than, &bound, &idx)) {
        printf("first over %" PRId32 " at %zu\n", bound, idx); // first over 3 at 0
    }

    printf("%zu of them are over %" PRId32 "\n",
           tda_span_count_if(nums, greater_than, &bound), bound); // 2 of them are over 3

    // a miss is an answer, not an error, and it leaves 'idx' alone
    printf("%s\n", tda_span_contains(nums, &(int32_t){7}, tda_eq_i32) ? "7 is in" : "no 7");
    /// [find]

    /// [sorted]
    // a search is a binary descent, so the span has to be sorted by the same cmp
    const tda_Span sorted = TDA_SPAN_OF(int32_t, 1, 2, 2, 2, 5, 9);

    if (tda_span_binary_search(sorted, &(int32_t){5}, tda_cmp_i32, &idx)) {
        printf("5 at %zu\n", idx); // 5 at 4
    }

    // with duplicates the honest answer is a range, not one index
    const tda_Range r = tda_span_equal_range(sorted, &(int32_t){2}, tda_cmp_i32);
    printf("the 2s are [%zu, %zu)\n", r.lo, r.hi); // the 2s are [1, 4)

    const tda_MinMax mm = tda_span_minmax_elem(sorted, tda_cmp_i32);
    printf("min at %zu, max at %zu\n", mm.min, mm.max); // min at 0, max at 5
    /// [sorted]

    return 0;
}
