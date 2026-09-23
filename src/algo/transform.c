#include "tda/algo/transform.h"

#include "tda/core/check.h"

#include <assert.h>

void tda_span_transform(tda_SpanMut dst, tda_Span src, tda_UnOp op, void *ctx) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(dst.len == src.len);
    assert(op);

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

    for (size_t i = 0; i < a.len; ++i) {
        op(tda_span_get_mut(dst, i), tda_span_get(a, i), tda_span_get(b, i), ctx);
    }
}
