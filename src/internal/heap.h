#pragma once

#include "tda/core/cmp.h"
#include "tda/core/span.h"

#include <assert.h>
#include <stddef.h>

/*
 * The sift that algo/heap and ds/pqueue share. pop_heap is a swap and then this sift, and
 * tda_pqueue_replace_top is a write and then this sift. Spelled through the public heap
 * ops, the replacement would need a pop_heap and a push_heap — two sifts where one does.
 */

/// walks 'idx' towards the leaves of the heap held in the first 'len' elems.
/// 'len' is a parameter rather than s.len because pop_heap and sort_heap shrink
/// the heap while leaving the popped elems in the span behind it
static inline void tda_heap_sift_down(tda_SpanMut s, size_t idx, size_t len, tda_Cmp cmp) {
    assert(len <= s.len);

    const tda_Span cs = tda_span_mut_to_span(s);

    for (;;) {
        const size_t left = 2 * idx + 1;
        if (left >= len) {
            break;
        }

        // on a tie the left child wins, which is what keeps pop_heap from
        // reshuffling equal elems for no reason
        size_t best = left;
        const size_t right = left + 1;
        if (right < len && cmp(tda_span_get(cs, left), tda_span_get(cs, right)) < 0) {
            best = right;
        }

        if (cmp(tda_span_get(cs, idx), tda_span_get(cs, best)) >= 0) {
            break;
        }

        tda_span_swap_elems(s, idx, best);
        idx = best;
    }
}
