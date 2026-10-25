// for @snippet

#include "tda/algo/compare.h"
#include "tda/core/cmp.h"
#include "tda/core/span.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [compare]
    const tda_Span a = TDA_SPAN_OF(int32_t, 1, 2, 3), b = TDA_SPAN_OF(int32_t, 1, 2, 4);

    printf("%d %d\n", tda_span_eq(a, b), tda_span_eq_by(a, b, tda_eq_i32, nullptr)); // 0 0

    size_t idx;
    if (tda_span_mismatch(a, b, tda_eq_i32, nullptr, &idx)) {
        printf("they part at %zu\n", idx); // they part at 2
    }

    // dictionary order: the first differing pair decides, and a prefix orders first
    const tda_Span shorter = TDA_SPAN_OF(int32_t, 1, 2);
    printf(
        "%d %d\n",
        tda_span_cmp(a, b, tda_cmp_i32, nullptr),
        tda_span_cmp(shorter, a, tda_cmp_i32, nullptr)
    ); // -1 -1

    // the same elems in another order, each as many times
    const tda_Span shuffled = TDA_SPAN_OF(int32_t, 3, 1, 2), skewed = TDA_SPAN_OF(int32_t, 3, 1, 1);
    printf(
        "%d %d\n",
        tda_span_is_permutation(a, shuffled, tda_eq_i32, nullptr),
        tda_span_is_permutation(a, skewed, tda_eq_i32, nullptr)
    ); // 1 0
    /// [compare]

    return 0;
}
