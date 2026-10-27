#pragma once

#include "tda/core/span.h"

/*
 * The overlap rule of algo/transform, the one op whose output may take another layout
 * than its input. tda_span_overlap and tda_span_apart_or_same, which the rest of algo
 * asserts with, are public in core/span.
 */

/// whether a destination may take a source of another layout: apart, or the very same
/// elems with the destination's no wider, so each position is read before it is written
[[nodiscard]]
static inline bool tda_span_may_write_over(tda_SpanMut dst, tda_Span src) {
    if (!tda_span_overlap(tda_span_mut_to_span(dst), src)) {
        return true; // apart, or nothing to write
    }

    return dst.data == src.data && dst.elem.size <= src.elem.size;
}
