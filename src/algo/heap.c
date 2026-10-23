#include "tda/algo/heap.h"

#include "tda/core/check.h"

#include "internal/heap.h"

#include <assert.h>

/* ========== build ========== */

void tda_span_make_heap(tda_SpanMut s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    // leaves are already heaps, so start at the last elem that has a child
    for (size_t i = s.len / 2; i > 0; --i) {
        tda_heap_sift_down(s, i - 1, s.len, cmp, ctx);
    }
}

/* ========== push / pop ========== */

void tda_span_push_heap(tda_SpanMut s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    tda_heap_sift_up(s, s.len - 1, cmp, ctx);
}

void tda_span_pop_heap(tda_SpanMut s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    if (s.len == 1) {
        return;
    }

    tda_span_swap_elems(s, 0, s.len - 1);
    tda_heap_sift_down(s, 0, s.len - 1, cmp, ctx);
}

/* ========== sort ========== */

void tda_span_sort_heap(tda_SpanMut s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    // each pop moves the largest to the end and shrinks the heap by one
    for (size_t len = s.len; len > 1; --len) {
        tda_span_pop_heap(tda_span_sub_mut(s, 0, len), cmp, ctx);
    }
}

/* ========== predicates ========== */

bool tda_span_is_heap(tda_Span s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    return tda_span_is_heap_until(s, cmp, ctx) == s.len;
}

size_t tda_span_is_heap_until(tda_Span s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    // every elem but the root is a child, so checking each against its parent covers
    // every edge once
    for (size_t child = 1; child < s.len; ++child) {
        if (!tda_heap_above(s, tda_heap_parent(child), child, cmp, ctx)) {
            return child;
        }
    }

    return s.len;
}
