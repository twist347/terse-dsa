// for @snippet

#include "tda/algo/fill.h"
#include "tda/algo/permute.h"
#include "tda/core/print.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"

#include <stdint.h>
#include <stdio.h>

/// [gen]
// a tda_Gen that draws its elem instead of computing it. The generator rides in 'ctx',
// which is why algo needs no random fill of its own — this is it.
static void gen_roll(void *dst, size_t idx, void *ctx) {
    (void) idx;

    *(int32_t *) dst = tda_rng_i32_range(ctx, 1, 6);
}
/// [gen]

int main() {
    /// [seed]
    // a generator is a value: no allocator, no status, nothing to drop. The seed is the
    // whole state, so this run can be replayed exactly by passing the same one.
    tda_Rng rng = tda_rng_from_seed(2026);

    // two draws, two statements: the order a call's arguments are evaluated in is unspecified
    const int32_t first = tda_rng_i32_range(&rng, 1, 6);
    const int32_t second = tda_rng_i32_range(&rng, 1, 6);
    printf("%d %d\n", first, second); // 3 4
    /// [seed]

    /// [draw]
    // uniform over what each one names, with no modulo bias anywhere
    const size_t idx = tda_rng_idx(&rng, 10);      // an index into ten elems
    const double unit = tda_rng_f64(&rng);         // [0.0, 1.0)
    const double angle = tda_rng_f64_range(&rng, 0.0, 6.283185307179586);
    const bool coin = tda_rng_bool(&rng);

    printf("%zu %.3f %.3f %s\n", idx, unit, angle, coin ? "heads" : "tails"); // 7 0.864 0.190 heads
    /// [draw]

    /// [span]
    int32_t rolls[12];
    const tda_SpanMut all = TDA_SPAN_FROM_DATA_MUT(int32_t, rolls, 12);

    // filling and shuffling are algo's, and each takes the generator the way sort takes
    // a comparator
    tda_span_generate(all, gen_roll, &rng);
    tda_span_shuffle(all, &rng);

    // dealing three: only the front is settled, and it is a uniform sample of all twelve
    tda_span_shuffle_prefix(all, 3, &rng);
    tda_span_fprint(tda_span_sub(tda_span_mut_to_span(all), 0, 3), stdout, tda_fprint_i32); // [2, 6, 6]

    // one random elem needs no call of its own: an index and a get
    const tda_Span rolled = tda_span_mut_to_span(all);
    printf("%d\n", *(const int32_t *) tda_span_get(rolled, tda_rng_idx(&rng, 12))); // 5
    /// [span]

    return 0;
}
