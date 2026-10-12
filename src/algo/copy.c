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
    assert(pred);

    // the count is known only at the end, so tda_span_get_mut checks room on each write;
    // 'dst' may be shorter than 'src' as long as the passing elems fit
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

size_t tda_span_sample(tda_SpanMut dst, tda_Span src, tda_Rng *rng) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(dst.elem_size == src.elem_size);
    assert(rng);

    const size_t want = dst.len < src.len ? dst.len : src.len;
    size_t write = 0;

    // 'read' never runs past the end: once the elems left equal the elems still wanted,
    // the draw always hits and every remaining elem is taken
    for (size_t read = 0; write < want; ++read) {
        if (tda_rng_idx(rng, src.len - read) < want - write) {
            memcpy(tda_span_get_mut(dst, write), tda_span_get(src, read), dst.elem_size);
            ++write;
        }
    }

    return write;
}
