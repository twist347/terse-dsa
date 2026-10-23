// for @snippet

#include "tda/algo/fill.h"
#include "tda/core/print.h"
#include "tda/core/span.h"

#include <stdint.h>

/// [gen]
static void squares(void *dst, size_t idx, void *) {
    *(int32_t *) dst = (int32_t) (idx * idx);
}
/// [gen]

int main() {
    /// [fill]
    int32_t buf[5];
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, 5);

    tda_span_fill(s, &(int32_t){7});
    tda_span_mut_print(s, tda_fprint_i32); // [7, 7, 7, 7, 7]

    tda_span_fill_zero(s);
    tda_span_mut_print(s, tda_fprint_i32); // [0, 0, 0, 0, 0]

    tda_span_generate(s, squares, nullptr);
    tda_span_mut_print(s, tda_fprint_i32); // [0, 1, 4, 9, 16]
    /// [fill]

    return 0;
}
