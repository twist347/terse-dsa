#pragma once

#include "tda/core/cmp.h"
#include "tda/core/span.h"

#include <assert.h>
#include <stddef.h>

/*
 * The heap's shape, its order rule and the sifts, shared by algo/heap and ds/pqueue.
 * pop_heap is a swap and then a sift down, and tda_pqueue_replace_top is a write and then
 * the same sift. Spelled through the public heap ops, the replacement would need a
 * pop_heap and a push_heap — two sifts where one does.
 */

/* ========== shape and order ========== */

/// the parent of 'idx' in the tree laid out over the span
[[nodiscard]]
static inline size_t tda_heap_parent(size_t idx) {
    assert(idx > 0);

    return (idx - 1) / 2;
}

/// the left child of 'idx'; the right one follows it
[[nodiscard]]
static inline size_t tda_heap_left(size_t idx) {
    return 2 * idx + 1;
}

/// whether s[upper] may sit above s[lower]: the one order rule of the heap, which keeps
/// the greatest elem at the root
[[nodiscard]]
static inline bool tda_heap_above(tda_Span s, size_t upper, size_t lower, tda_Cmp cmp, void *ctx) {
    return cmp(tda_span_get(s, upper), tda_span_get(s, lower), ctx) >= 0;
}

/* ========== sifts ========== */

/// walks 'idx' towards the leaves of the heap held in the first 'len' elems.
/// 'len' is a parameter rather than s.len because pop_heap and sort_heap shrink
/// the heap while leaving the popped elems in the span behind it
static inline void tda_heap_sift_down(tda_SpanMut s, size_t idx, size_t len, tda_Cmp cmp, void *ctx) {
    assert(len <= s.len);

    const tda_Span cs = tda_span_mut_to_span(s);

    for (;;) {
        const size_t left = tda_heap_left(idx);
        if (left >= len) {
            break;
        }

        // on a tie the left child wins, so pop_heap does not reshuffle equal elems
        // for no reason
        const size_t right = left + 1;
        const size_t best = right < len && !tda_heap_above(cs, left, right, cmp, ctx) ? right : left;

        if (tda_heap_above(cs, idx, best, cmp, ctx)) {
            break;
        }

        tda_span_swap_elems(s, idx, best);
        idx = best;
    }
}

/// walks 'idx' towards the root while it may not sit below its parent
static inline void tda_heap_sift_up(tda_SpanMut s, size_t idx, tda_Cmp cmp, void *ctx) {
    const tda_Span cs = tda_span_mut_to_span(s);

    while (idx > 0) {
        const size_t parent = tda_heap_parent(idx);
        if (tda_heap_above(cs, parent, idx, cmp, ctx)) {
            break;
        }

        tda_span_swap_elems(s, parent, idx);
        idx = parent;
    }
}
