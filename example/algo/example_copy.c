// for @snippet

#include "tda/algo/copy.h"
#include "tda/core/print.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"

#include <stdint.h>
#include <stdio.h>

/// [pred]
static bool is_even(const void *elem, void *) {
    return *(const int32_t *) elem % 2 == 0;
}
/// [pred]

int main() {
    /// [copy]
    const tda_Span src = TDA_SPAN_OF(int32_t, 1, 2, 3, 4, 5);

    int32_t buf[5];
    const tda_SpanMut dst = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    tda_span_copy(dst, src);
    tda_span_mut_print(dst, tda_fprint_i32, nullptr); // [1, 2, 3, 4, 5]

    // copy_if packs the passing elems to the front and returns their count; the rest of
    // 'dst' keeps its old contents
    const size_t kept = tda_span_copy_if(dst, src, is_even, nullptr);
    printf("%zu kept\n", kept); // 2 kept
    tda_span_mut_print(tda_span_sub_mut(dst, 0, kept), tda_fprint_i32, nullptr); // [2, 4]

    // the two may overlap, as with memmove
    const tda_SpanMut all = TDA_SPAN_OF_MUT(int32_t, 1, 2, 3, 4, 5);
    tda_span_copy(tda_span_sub_mut(all, 1, 4), tda_span_sub(tda_span_mut_to_span(all), 0, 4));
    tda_span_mut_print(all, tda_fprint_i32, nullptr); // [1, 1, 2, 3, 4]
    /// [copy]

    /// [sample]
    // picks as many as 'dst' holds, none twice, in source order; the same seed replays
    // the same pick
    tda_Rng rng = tda_rng_from_seed(7);

    int32_t pick[3];
    const tda_SpanMut picked = TDA_SPAN_FROM_DATA_MUT(int32_t, pick, 3);
    const size_t n = tda_span_sample(picked, src, &rng);
    tda_span_mut_print(tda_span_sub_mut(picked, 0, n), tda_fprint_i32, nullptr); // [1, 2, 4]
    /// [sample]

    return 0;
}
