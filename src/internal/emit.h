#pragma once

#include "tda/core/check.h"
#include "tda/core/span.h"

#include <string.h>

/*
 * Appending into a span that is filled left to right, for the algorithms that walk two
 * sorted spans at once — algo/merge and algo/set. Both take the current write position
 * and report the next one, so the caller carries one cursor instead of two.
 */

/// appends one elem to 'dst' at 'out' and reports the next write position
[[nodiscard]]
static inline size_t tda_emit(tda_SpanMut dst, size_t out, const void *elem) {
    memcpy(tda_span_get_mut(dst, out), elem, dst.elem_size);

    return out + 1;
}

/// appends src[from..] and reports the next write position
[[nodiscard]]
static inline size_t tda_emit_rest(tda_SpanMut dst, size_t out, tda_Span src, size_t from) {
    if (from >= src.len) {
        return out;
    }

    // tda_span_get_mut vouches only for the first slot; the run has to fit as a whole
    const size_t n = src.len - from;
    void *to = tda_span_get_mut(dst, out);
    TDA_EXPECT(n <= dst.len - out);
    memcpy(to, tda_span_get(src, from), n * src.elem_size);

    return out + n;
}
