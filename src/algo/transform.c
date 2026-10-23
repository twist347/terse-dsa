#include "tda/algo/transform.h"

#include "tda/core/check.h"

#include "internal/overlap.h"

#include <assert.h>

/// what the header allows of a destination and a source: apart, or the very same elems
/// with the destination's no wider, so each position is read before it is written
[[nodiscard]] [[maybe_unused]]
static bool may_write_over(tda_SpanMut dst, tda_Span src) {
    if (dst.data == src.data) {
        return dst.elem.size <= src.elem.size;
    }

    return !tda_span_overlap(tda_span_mut_to_span(dst), src);
}

void tda_span_transform(tda_SpanMut dst, tda_Span src, tda_UnOp op, void *ctx) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(dst.len == src.len);
    assert(op);
    assert(may_write_over(dst, src));

    for (size_t i = 0; i < src.len; ++i) {
        op(tda_span_get_mut(dst, i), tda_span_get(src, i), ctx);
    }
}

void tda_span_zip_with(tda_SpanMut dst, tda_Span a, tda_Span b, tda_BinOp op, void *ctx) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    TDA_EXPECT(dst.len == a.len);
    TDA_EXPECT(a.len == b.len);
    assert(op);
    assert(may_write_over(dst, a));
    assert(may_write_over(dst, b));

    for (size_t i = 0; i < a.len; ++i) {
        op(tda_span_get_mut(dst, i), tda_span_get(a, i), tda_span_get(b, i), ctx);
    }
}
