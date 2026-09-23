#include "tda/algo/compare.h"

#include "tda/core/check.h"

#include <assert.h>
#include <string.h>

/* ========== equality ========== */

bool tda_span_eq(tda_Span a, tda_Span b) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    TDA_EXPECT(a.elem_size == b.elem_size);

    if (a.len != b.len) {
        return false;
    }

    if (a.len == 0 || a.data == b.data) {
        return true;
    }

    return memcmp(a.data, b.data, a.len * a.elem_size) == 0;
}

bool tda_span_eq_by(tda_Span a, tda_Span b, tda_Eq eq) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    TDA_EXPECT(a.elem_size == b.elem_size);
    assert(eq);

    if (a.len != b.len) {
        return false;
    }

    if (a.len == 0 || a.data == b.data) {
        return true;
    }

    for (size_t i = 0; i < a.len; ++i) {
        const void *x = tda_span_get(a, i);
        const void *y = tda_span_get(b, i);
        if (!eq(x, y)) {
            return false;
        }
    }
    return true;
}

bool tda_span_mismatch(tda_Span a, tda_Span b, tda_Eq eq, size_t *out_idx) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(eq);
    assert(out_idx);
    TDA_EXPECT(a.elem_size == b.elem_size);

    if (a.data == b.data && a.len == b.len) {
        return false;
    }

    const size_t common = a.len < b.len ? a.len : b.len;

    for (size_t i = 0; i < common; ++i) {
        if (!eq(tda_span_get(a, i), tda_span_get(b, i))) {
            *out_idx = i;
            return true;
        }
    }
    return false;
}

/* ========== ordering ========== */

int tda_span_cmp(tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(a.elem_size == b.elem_size);

    if (a.data == b.data && a.len == b.len) {
        return 0;
    }

    const size_t common = a.len < b.len ? a.len : b.len;

    for (size_t i = 0; i < common; ++i) {
        const int c = cmp(tda_span_get(a, i), tda_span_get(b, i));
        if (c != 0) {
            return c < 0 ? -1 : 1;
        }
    }
    return (a.len > b.len) - (a.len < b.len);
}
