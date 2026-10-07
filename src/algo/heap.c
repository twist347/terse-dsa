#include "tda/algo/heap.h"

#include "tda/core/check.h"

#include "internal/heap.h"

#include <assert.h>

/* ========== internals ========== */

/// walks 'idx' towards the root while it outranks its parent
static void sift_up(tda_SpanMut s, size_t idx, tda_Cmp cmp);

/* ========== build ========== */

void tda_span_make_heap(tda_SpanMut s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    // leaves are heaps already, so the work starts at the last elem that has a child
    for (size_t i = s.len / 2; i > 0; --i) {
        tda_heap_sift_down(s, i - 1, s.len, cmp);
    }
}

/* ========== push / pop ========== */

void tda_span_push_heap(tda_SpanMut s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    sift_up(s, s.len - 1, cmp);
}

void tda_span_pop_heap(tda_SpanMut s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    if (s.len == 1) {
        return;
    }

    tda_span_swap_elems(s, 0, s.len - 1);
    tda_heap_sift_down(s, 0, s.len - 1, cmp);
}

/* ========== sort ========== */

void tda_span_sort_heap(tda_SpanMut s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    // each pop parks one more largest elem behind a heap one shorter
    for (size_t len = s.len; len > 1; --len) {
        tda_span_pop_heap(tda_span_sub_mut(s, 0, len), cmp);
    }
}

/* ========== predicates ========== */

bool tda_span_is_heap(tda_Span s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    return tda_span_is_heap_until(s, cmp) == s.len;
}

size_t tda_span_is_heap_until(tda_Span s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    // every elem but the root is somebody's child, so checking each child against its
    // parent covers every edge of the tree exactly once
    for (size_t child = 1; child < s.len; ++child) {
        const size_t parent = (child - 1) / 2;

        if (cmp(tda_span_get(s, parent), tda_span_get(s, child)) < 0) {
            return child;
        }
    }

    return s.len;
}

/* ========== internals ========== */

static void sift_up(tda_SpanMut s, size_t idx, tda_Cmp cmp) {
    const tda_Span cs = tda_span_mut_to_span(s);

    while (idx > 0) {
        const size_t parent = (idx - 1) / 2;

        if (cmp(tda_span_get(cs, parent), tda_span_get(cs, idx)) >= 0) {
            break;
        }

        tda_span_swap_elems(s, parent, idx);
        idx = parent;
    }
}
