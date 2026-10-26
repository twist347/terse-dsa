// for @snippet

#include "tda/algo/modify.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [modify]
    // a span cannot resize, so ops that drop elems pack the kept ones to the front and
    // return the new length; the tail is left unspecified
    const tda_SpanMut s = TDA_SPAN_OF_MUT(int32_t, 1, 1, 2, 2, 2, 3);

    // unique compares only adjacent elems, so the result is a set only on a sorted span
    const size_t kept = tda_span_unique(s, tda_eq_i32, nullptr);
    printf("%zu kept, ", kept);
    tda_span_mut_print(tda_span_sub_mut(s, 0, kept), tda_fprint_i32, nullptr); // 3 kept, [1, 2, 3]

    const size_t left = TDA_SPAN_REMOVE(int32_t, tda_span_sub_mut(s, 0, kept), 2, tda_eq_i32, nullptr);
    printf("%zu left, ", left);
    tda_span_mut_print(tda_span_sub_mut(s, 0, left), tda_fprint_i32, nullptr); // 2 left, [1, 3]

    // replace keeps the length, so it returns nothing
    const tda_SpanMut repl = TDA_SPAN_OF_MUT(int32_t, 1, 2, 1, 3);
    TDA_SPAN_REPLACE(int32_t, int32_t, repl, 1, 9, tda_eq_i32, nullptr); // key type, elem type
    tda_span_mut_print(repl, tda_fprint_i32, nullptr); // [9, 2, 9, 3]
    /// [modify]

    return 0;
}
