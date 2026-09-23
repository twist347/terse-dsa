#include "tda/algo/copy.h"

#include "tda/core/check.h"

#include <assert.h>
#include <string.h>

void tda_span_copy(tda_SpanMut dst, tda_Span src) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(dst.elem_size == src.elem_size);
    TDA_EXPECT(dst.len == src.len);

    if (dst.len == 0 || dst.data == src.data) {
        return;
    }

    memcpy(dst.data, src.data, dst.len * dst.elem_size);
}

size_t tda_span_copy_if(tda_SpanMut dst, tda_Span src, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(dst.elem_size == src.elem_size);
    TDA_EXPECT(dst.len >= src.len);
    assert(pred);

    size_t write = 0;

    for (size_t read = 0; read < src.len; ++read) {
        const void *cur = tda_span_get(src, read);
        if (pred(cur, ctx)) {
            memcpy(tda_span_get_mut(dst, write), cur, dst.elem_size);
            ++write;
        }
    }

    return write;
}

void tda_span_copy_overlapping(tda_SpanMut dst, tda_Span src) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(dst.elem_size == src.elem_size);
    TDA_EXPECT(dst.len == src.len);

    if (dst.len == 0 || dst.data == src.data) {
        return;
    }

    memmove(dst.data, src.data, dst.len * dst.elem_size);
}
