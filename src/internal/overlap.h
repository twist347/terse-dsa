#pragma once

#include "tda/core/span.h"

#include <stdint.h>

/*
 * Overlap checks for the algorithms whose result is wrong, not unsafe, when an output
 * shares memory with an input: merge, the set ops, copy, the scans and transform. A breach
 * is a wrong answer, so callers assert rather than TDA_EXPECT.
 */

/// whether two views share a byte. The addresses are compared as uintptr_t, since '<'
/// between pointers into different objects is undefined
[[nodiscard]]
static inline bool tda_span_overlap(tda_Span a, tda_Span b) {
    if (a.len == 0 || b.len == 0) {
        return false;
    }

    const uintptr_t pa = (uintptr_t) a.data;
    const uintptr_t pb = (uintptr_t) b.data;

    return pa < pb + b.len * b.elem.size && pb < pa + a.len * a.elem.size;
}

/// whether two views of one len and elem layout are either apart or the very same elems,
/// the two cases an elementwise op can take
[[nodiscard]]
static inline bool tda_span_apart_or_same(tda_Span a, tda_Span b) {
    return a.data == b.data || !tda_span_overlap(a, b);
}

/// whether a destination may take a source of another layout: apart, or the very same
/// elems with the destination's no wider, so each position is read before it is written
[[nodiscard]]
static inline bool tda_span_may_write_over(tda_SpanMut dst, tda_Span src) {
    if (dst.data == src.data) {
        return dst.elem.size <= src.elem.size;
    }

    return !tda_span_overlap(tda_span_mut_to_span(dst), src);
}
