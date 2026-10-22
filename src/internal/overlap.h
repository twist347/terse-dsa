#pragma once

#include "tda/core/span.h"

#include <stdint.h>

/*
 * Overlap checks for the algorithms whose result is wrong, not unsafe, when an output
 * shares memory with an input: merge, the set ops, copy and the scans. A breach is a
 * wrong answer, so callers assert rather than TDA_EXPECT.
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

    return pa < pb + b.len * b.elem_size && pb < pa + a.len * a.elem_size;
}

/// whether two views of one len and elem_size are either apart or the very same elems,
/// the two cases an elementwise op can take
[[nodiscard]]
static inline bool tda_span_apart_or_same(tda_Span a, tda_Span b) {
    return a.data == b.data || !tda_span_overlap(a, b);
}
