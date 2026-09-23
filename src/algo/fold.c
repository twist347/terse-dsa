#include "tda/algo/fold.h"

#include "tda/core/check.h"

#include <assert.h>
#include <string.h>

/* ========== fold ========== */

void tda_span_fold(tda_Span s, void *acc, tda_Fold fold, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(acc);
    assert(fold);

    for (size_t i = 0; i < s.len; ++i) {
        fold(acc, tda_span_get(s, i), ctx);
    }
}

void tda_span_fold_back(tda_Span s, void *acc, tda_Fold fold, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(acc);
    assert(fold);

    for (size_t i = s.len; i > 0; --i) {
        fold(acc, tda_span_get(s, i - 1), ctx);
    }
}

/* ========== scan ========== */

void tda_span_partial_sum(tda_SpanMut dst, tda_Span src, tda_BinOp op, void *ctx) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(dst.elem_size == src.elem_size);
    TDA_EXPECT(dst.len == src.len);
    assert(op);

    if (src.len == 0) {
        return;
    }

    memcpy(tda_span_get_mut(dst, 0), tda_span_get(src, 0), dst.elem_size);

    const tda_Span prev = tda_span_mut_to_span(dst);

    for (size_t i = 1; i < src.len; ++i) {
        op(tda_span_get_mut(dst, i), tda_span_get(prev, i - 1), tda_span_get(src, i), ctx);
    }
}

void tda_span_adjacent_difference(tda_SpanMut dst, tda_Span src, tda_BinOp op, void *ctx) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(src);
    TDA_EXPECT(dst.elem_size == src.elem_size);
    TDA_EXPECT(dst.len == src.len);
    assert(op);

    if (src.len == 0) {
        return;
    }

    memcpy(tda_span_get_mut(dst, 0), tda_span_get(src, 0), dst.elem_size);

    for (size_t i = 1; i < src.len; ++i) {
        op(tda_span_get_mut(dst, i), tda_span_get(src, i), tda_span_get(src, i - 1), ctx);
    }
}
