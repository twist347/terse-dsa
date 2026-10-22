#include "tda/algo/set.h"

#include "tda/core/check.h"

#include "internal/emit.h"
#include "internal/overlap.h"

#include <assert.h>

/* ========== set ops ========== */

size_t tda_span_set_union(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(dst.elem_size == a.elem_size);
    TDA_EXPECT(dst.elem_size == b.elem_size);
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), a));
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), b));
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
            // one copy of the pair, taken from 'a'; any leftover run on either side is
            // emitted on later turns, which makes the count max(m, n)
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
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), a));
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), b));
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
            // the copy comes from 'a': equal by 'cmp' does not mean identical, so the
            // side an elem comes from is observable and must be fixed
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
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), a));
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), b));
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
            // one copy in 'a' cancels one copy in 'b'
            ++i;
            ++j;
        }
    }

    // what is left in 'b' has nothing in 'a' to cancel
    return tda_emit_rest(dst, out, a, i);
}

size_t tda_span_set_symmetric_difference(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(dst.elem_size == a.elem_size);
    TDA_EXPECT(dst.elem_size == b.elem_size);
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), a));
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), b));
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
            // the pair cancels; the excess of a longer run is emitted on later turns,
            // which makes the count |m - n|
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

        // advance 'sup' to the elem being matched; running out or stepping past the
        // value means this copy has no match
        while (i < sup.len && cmp(tda_span_get(sup, i), want) < 0) {
            ++i;
        }

        if (i == sup.len || cmp(tda_span_get(sup, i), want) > 0) {
            return false;
        }

        // matched: that copy is used up, so duplicates need duplicates
        ++i;
    }

    return true;
}
