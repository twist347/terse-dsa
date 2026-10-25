// for @snippet

#include "tda/algo/fill.h"
#include "tda/algo/permute.h"
#include "tda/core/print.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/// [gen]
// a tda_Gen that draws its elem instead of computing it. The generator rides in 'ctx',
// so algo needs no separate random fill
static void gen_roll(void *dst, size_t, void *ctx) {
    *(int32_t *) dst = tda_rng_i32_range(ctx, 1, 6);
}
/// [gen]

int main() {
    /// [seed]
    // a generator is a plain value: no allocator, no status, nothing to drop. The seed is
    // the whole state, so the same seed replays this run exactly
    tda_Rng rng = tda_rng_from_seed(2026);

    // two draws, two statements: argument evaluation order is unspecified
    const int32_t first = tda_rng_i32_range(&rng, 1, 6), second = tda_rng_i32_range(&rng, 1, 6);
    printf("%" PRId32 " %" PRId32 "\n", first, second); // 3 4
    /// [seed]

    /// [draw]
    // uniform over each range, with no modulo bias
    const size_t idx = tda_rng_idx(&rng, 10);      // an index into ten elems
    const double unit = tda_rng_f64(&rng);         // [0.0, 1.0)
    const double angle = tda_rng_f64_range(&rng, 0.0, 6.283185307179586);
    const bool coin = tda_rng_bool(&rng);

    printf("%zu %.3f %.3f %s\n", idx, unit, angle, coin ? "heads" : "tails"); // 7 0.864 0.190 heads
    /// [draw]

    /// [span]
    int32_t rolls[12];
    const tda_SpanMut all = TDA_SPAN_FROM_DATA_MUT(int32_t, rolls, 12);

    // filling and shuffling are in algo, taking the generator as sort takes a cmp
    tda_span_generate(all, gen_roll, &rng);
    tda_span_shuffle(all, &rng);

    // dealing three: only the front is settled, a uniform sample of all twelve
    tda_span_shuffle_prefix(all, 3, &rng);
    tda_span_fprint(tda_span_sub(tda_span_mut_to_span(all), 0, 3), stdout, tda_fprint_i32); // [2, 6, 6]

    // a random elem is just a random index and a get
    const tda_Span rolled = tda_span_mut_to_span(all);
    printf("%" PRId32 "\n", *(const int32_t *) tda_span_get(rolled, tda_rng_idx(&rng, 12))); // 5
    /// [span]

    return 0;
}
