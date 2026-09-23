#include "tda/algo/set.h"

#include "tda/core/check.h"

#include "internal/emit.h"

#include <assert.h>

/* ========== set ops ========== */

size_t tda_span_set_union(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(dst.elem_size == a.elem_size);
    TDA_EXPECT(dst.elem_size == b.elem_size);
    TDA_EXPECT(dst.len >= a.len + b.len);

    size_t i = 0, j = 0;
    size_t out = 0;

    while (i < a.len && j < b.len) {
        const void *l = tda_span_get(a, i);
        const void *r = tda_span_get(b, j);
        const int c = cmp(l, r);

        if (c < 0) {
            out = tda_emit(dst, out, l);
            ++i;
        } else if (c > 0) {
            out = tda_emit(dst, out, r);
            ++j;
        } else {
            // one copy of the pair, taken from 'a'; a run left over on either side is
            // picked up by the turns that follow, which is what makes the count max(m, n)
            out = tda_emit(dst, out, l);
            ++i;
            ++j;
        }
    }

    out = tda_emit_rest(dst, out, a, i);
    out = tda_emit_rest(dst, out, b, j);

    return out;
}

size_t tda_span_set_intersection(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(dst.elem_size == a.elem_size);
    TDA_EXPECT(dst.elem_size == b.elem_size);
    TDA_EXPECT(dst.len >= (a.len < b.len ? a.len : b.len));

    size_t i = 0, j = 0;
    size_t out = 0;

    while (i < a.len && j < b.len) {
        const void *l = tda_span_get(a, i);
        const void *r = tda_span_get(b, j);
        const int c = cmp(l, r);

        if (c < 0) {
            ++i;
        } else if (c > 0) {
            ++j;
        } else {
            // the copy handed out is 'a's: equal by 'cmp' does not mean identical, so
            // which side an elem comes from is observable and has to be a decision
            out = tda_emit(dst, out, l);
            ++i;
            ++j;
        }
    }

    return out;
}

size_t tda_span_set_difference(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(dst.elem_size == a.elem_size);
    TDA_EXPECT(dst.elem_size == b.elem_size);
    TDA_EXPECT(dst.len >= a.len);

    size_t i = 0, j = 0;
    size_t out = 0;

    while (i < a.len && j < b.len) {
        const void *l = tda_span_get(a, i);
        const void *r = tda_span_get(b, j);
        const int c = cmp(l, r);

        if (c < 0) {
            out = tda_emit(dst, out, l);
            ++i;
        } else if (c > 0) {
            ++j;
        } else {
            // one copy of 'a' spent against one copy of 'b'
            ++i;
            ++j;
        }
    }

    // whatever is left in 'b' cancels nothing: it has no counterpart left in 'a'
    return tda_emit_rest(dst, out, a, i);
}

size_t tda_span_set_symmetric_difference(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(dst.elem_size == a.elem_size);
    TDA_EXPECT(dst.elem_size == b.elem_size);
    TDA_EXPECT(dst.len >= a.len + b.len);

    size_t i = 0, j = 0;
    size_t out = 0;

    while (i < a.len && j < b.len) {
        const void *l = tda_span_get(a, i);
        const void *r = tda_span_get(b, j);
        const int c = cmp(l, r);

        if (c < 0) {
            out = tda_emit(dst, out, l);
            ++i;
        } else if (c > 0) {
            out = tda_emit(dst, out, r);
            ++j;
        } else {
            // the pair cancels; a longer run on one side survives by this same rule on
            // the turns that follow, which is what makes the count |m - n|
            ++i;
            ++j;
        }
    }

    out = tda_emit_rest(dst, out, a, i);
    out = tda_emit_rest(dst, out, b, j);

    return out;
}

/* ========== predicates ========== */

bool tda_span_includes(tda_Span sup, tda_Span sub, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(sup);
    TDA_SPAN_ASSERT(sub);
    assert(cmp);
    TDA_EXPECT(sup.elem_size == sub.elem_size);

    size_t i = 0;

    for (size_t j = 0; j < sub.len; ++j) {
        const void *want = tda_span_get(sub, j);

        // walk 'sup' up to the elem being accounted for; running out of it, or stepping
        // past the value, both mean this copy has no match left
        while (i < sup.len && cmp(tda_span_get(sup, i), want) < 0) {
            ++i;
        }

        if (i == sup.len || cmp(tda_span_get(sup, i), want) > 0) {
            return false;
        }

        // matched: that copy of it is spent, so duplicates need duplicates
        ++i;
    }

    return true;
}
