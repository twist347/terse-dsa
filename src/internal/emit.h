#pragma once

#include "tda/core/check.h"
#include "tda/core/span.h"

#include <string.h>

/*
 * Appending into a span that is filled left to right: the algorithms that walk two sorted
 * spans at once (algo/merge, algo/set) and those that pick elems out of one (copy_if,
 * sample). Each helper takes the current write position and reports the next one, so the
 * caller carries one cursor instead of two.
 */

/// appends one elem to 'dst' at 'out' and reports the next write position
[[nodiscard]]
static inline size_t tda_emit(tda_SpanMut dst, size_t out, const void *elem) {
    memcpy(tda_span_get_mut(dst, out), elem, dst.elem.size);

    return out + 1;
}

/// appends src[from..] and reports the next write position
[[nodiscard]]
static inline size_t tda_emit_rest(tda_SpanMut dst, size_t out, tda_Span src, size_t from) {
    if (from >= src.len) {
        return out;
    }

    // tda_span_get_mut checks only the first slot; the whole run must fit
    const size_t n = src.len - from;
    void *to = tda_span_get_mut(dst, out);
    TDA_EXPECT(n <= dst.len - out);
    memcpy(to, tda_span_get(src, from), n * src.elem.size);

    return out + n;
}
