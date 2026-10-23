// for @snippet

#include "tda/algo/permute.h"
#include "tda/alloc/default.h"
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
    /// [move]
    const tda_SpanMut s = TDA_SPAN_OF_MUT(int32_t, 1, 2, 3, 4, 5);

    tda_span_reverse(s);
    tda_span_mut_print(s, tda_fprint_i32); // [5, 4, 3, 2, 1]

    // rotate left so that the elem at 'mid' becomes the first
    tda_span_rotate(s, 2);
    tda_span_mut_print(s, tda_fprint_i32); // [3, 2, 1, 5, 4]
    /// [move]

    /// [shuffle]
    // the order comes from a generator, so the same seed replays the same shuffle
    tda_Rng rng = tda_rng_from_seed(2026);
    const tda_SpanMut deck = TDA_SPAN_OF_MUT(int32_t, 1, 2, 3, 4, 5, 6, 7, 8);

    tda_span_shuffle(deck, &rng);
    tda_span_mut_print(deck, tda_fprint_i32); // [8, 2, 3, 1, 6, 7, 5, 4]

    // dealing a hand: only the first three positions are settled, a uniform sample of
    // the whole span, not just its front
    tda_span_shuffle_prefix(deck, 3, &rng);
    tda_span_mut_print(tda_span_sub_mut(deck, 0, 3), tda_fprint_i32); // [4, 5, 6]
    /// [shuffle]

    /// [partition]
    const tda_SpanMut fast = TDA_SPAN_OF_MUT(int32_t, 1, 2, 3, 4, 5, 6);

    // the fast split swaps elems, so neither side keeps its original order
    const size_t fast_boundary = tda_span_partition(fast, is_even, nullptr);
    printf("%zu even, ", fast_boundary);
    tda_span_mut_print(fast, tda_fprint_i32); // 3 even, [2, 4, 6, 1, 5, 3]

    // the stable split needs a buffer, so it takes an allocator and can fail
    const tda_SpanMut stable = TDA_SPAN_OF_MUT(int32_t, 1, 2, 3, 4, 5, 6);
    size_t stable_boundary;
    const tda_Status st = tda_span_partition_stable(
        stable,
        is_even,
        nullptr,
        tda_al_default(),
        &stable_boundary
    );
    if (TDA_STATUS_IS_ERR(st)) {
        return 1;
    }
    printf("%zu even, ", stable_boundary);
    tda_span_mut_print(stable, tda_fprint_i32); // 3 even, [2, 4, 6, 1, 3, 5]
    /// [partition]

    return 0;
}
