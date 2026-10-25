#include "tda/algo/compare.h"

#include "tda/algo/search.h"
#include "tda/core/check.h"

#include <assert.h>
#include <string.h>

/* ========== equality ========== */

bool tda_span_eq(tda_Span a, tda_Span b) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    TDA_EXPECT(tda_layout_eq(a.elem, b.elem));

    if (a.len != b.len) {
        return false;
    }

    if (a.len == 0 || a.data == b.data) {
        return true;
    }

    return memcmp(a.data, b.data, tda_span_bytes(a)) == 0;
}

bool tda_span_eq_by(tda_Span a, tda_Span b, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(eq);
    TDA_EXPECT(tda_layout_eq(a.elem, b.elem));

    if (a.len != b.len) {
        return false;
    }

    if (a.len == 0 || a.data == b.data) {
        return true;
    }

    for (size_t i = 0; i < a.len; ++i) {
        const void *lhs = tda_span_get(a, i);
        const void *rhs = tda_span_get(b, i);
        if (!eq(lhs, rhs, ctx)) {
            return false;
        }
    }

    return true;
}

bool tda_span_mismatch(tda_Span a, tda_Span b, tda_Eq eq, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(eq);
    assert(out_idx);
    TDA_EXPECT(tda_layout_eq(a.elem, b.elem));

    if (a.data == b.data && a.len == b.len) {
        return false;
    }

    const size_t common = a.len < b.len ? a.len : b.len;

    for (size_t i = 0; i < common; ++i) {
        if (!eq(tda_span_get(a, i), tda_span_get(b, i), ctx)) {
            *out_idx = i;
            return true;
        }
    }

    return false;
}

bool tda_span_is_permutation(tda_Span a, tda_Span b, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(eq);
    TDA_EXPECT(tda_layout_eq(a.elem, b.elem));

    if (a.len != b.len) {
        return false;
    }

    // the common prefix is trivially a permutation of itself, so only the rest is
    // counted; with equal lengths and no mismatch the spans are equal
    size_t from;
    if (!tda_span_mismatch(a, b, eq, ctx, &from)) {
        return true;
    }

    const tda_Span rest_a = tda_span_sub(a, from, a.len - from);
    const tda_Span rest_b = tda_span_sub(b, from, b.len - from);

    for (size_t i = 0; i < rest_a.len; ++i) {
        const void *elem = tda_span_get(rest_a, i);

        // an elem seen earlier was already counted, all its copies at once
        if (tda_span_contains(tda_span_sub(rest_a, 0, i), elem, eq, ctx)) {
            continue;
        }

        // all its copies in 'a' lie at 'i' or later; a zero count in 'b' fails early
        const size_t in_b = tda_span_count(rest_b, elem, eq, ctx);
        if (in_b == 0 || in_b != tda_span_count(tda_span_sub(rest_a, i, rest_a.len - i), elem, eq, ctx)) {
            return false;
        }
    }

    return true;
}

/* ========== ordering ========== */

int tda_span_cmp(tda_Span a, tda_Span b, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(tda_layout_eq(a.elem, b.elem));

    if (a.data == b.data && a.len == b.len) {
        return 0;
    }

    const size_t common = a.len < b.len ? a.len : b.len;

    for (size_t i = 0; i < common; ++i) {
        const int c = cmp(tda_span_get(a, i), tda_span_get(b, i), ctx);
        if (c != 0) {
            return c < 0 ? -1 : 1;
        }
    }

    return (a.len > b.len) - (a.len < b.len);
}
