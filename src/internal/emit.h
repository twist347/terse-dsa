#pragma once

#include "tda/core/check.h"
#include "tda/core/span.h"

#include <string.h>

/*
 * Appending into a span that is filled left to right: the algorithms that walk two sorted
 * spans at once (algo/merge, algo/set), those that pick elems out of one (copy_if,
 * sample) and those that compact one in place (unique, remove_if, partition_stable). Each
 * helper takes the current write position and reports the next one, so the caller
 * carries one cursor instead of two.
 */

/// appends one elem to 'dst' at 'write' and reports the next write position
[[nodiscard]]
static inline size_t tda_emit(tda_SpanMut dst, size_t write, const void *elem) {
    memcpy(tda_span_get_mut(dst, write), elem, dst.elem.size);

    return write + 1;
}

/// tda_emit for compacting a span in place: 'elem' is an elem of 'dst' at or after
/// 'write', so it is either a whole other slot or already the one written
[[nodiscard]]
static inline size_t tda_emit_over(tda_SpanMut dst, size_t write, const void *elem) {
    void *to = tda_span_get_mut(dst, write);

    // still in its slot while nothing before it has been dropped
    if (to != elem) {
        memcpy(to, elem, dst.elem.size);
    }

    return write + 1;
}

/// appends src[from..] and reports the next write position
[[nodiscard]]
static inline size_t tda_emit_rest(tda_SpanMut dst, size_t write, tda_Span src, size_t from) {
    if (from >= src.len) {
        return write;
    }

    // tda_span_get_mut checks only the first slot; the whole run must fit
    const size_t n = src.len - from;
    void *to = tda_span_get_mut(dst, write);
    TDA_EXPECT(n <= dst.len - write);
    memcpy(to, tda_span_get(src, from), n * src.elem.size);

    return write + n;
}
