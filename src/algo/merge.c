#include "tda/algo/merge.h"

#include "tda/algo/permute.h"
#include "tda/algo/search.h"
#include "tda/core/check.h"

#include "internal/emit.h"
#include "internal/order.h"
#include "internal/ptr.h"

#include <assert.h>
#include <string.h>

/* ========== internals ========== */

static void merge_in_place(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx);

/// merges in one linear pass, with the shorter run copied into 'buf'
static void merge_buffered(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx, void *buf);

/// the left run is in 'parked': fill 's' from the front
static void merge_forward(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx, tda_Span parked);

/// the right run is in 'parked': fill 's' from the back
static void merge_backward(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx, tda_Span parked);

/* ========== merge ========== */

void tda_span_merge(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(tda_layout_eq(dst.elem, a.elem));
    TDA_EXPECT(tda_layout_eq(dst.elem, b.elem));
    TDA_EXPECT(dst.len == a.len + b.len);
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), a));
    assert(!tda_span_overlap(tda_span_mut_to_span(dst), b));

    size_t i = 0, j = 0;
    size_t write = 0;

    while (i < a.len && j < b.len) {
        const void *lhs = tda_span_get(a, i), *rhs = tda_span_get(b, j);

        // a tie takes from 'a', keeping equal elems in order; this is what makes
        // tda_span_sort_stable stable
        if (!tda_right_goes_first(cmp, ctx, lhs, rhs)) {
            write = tda_emit(dst, write, lhs);
            ++i;
        } else {
            write = tda_emit(dst, write, rhs);
            ++j;
        }
    }

    // at most one of the two has elems left; either may be the one
    write = tda_emit_rest(dst, write, a, i);
    write = tda_emit_rest(dst, write, b, j);

    assert(write == dst.len);
}

/* ========== inplace merge ========== */

void tda_span_inplace_merge(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx, tda_Al *al) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(mid <= s.len);

    const size_t left_len = mid, right_len = s.len - mid;

    if (left_len == 0 || right_len == 0) {
        return;
    }

    // the buffer holds the shorter run, so at most half the span
    const size_t buf_len = left_len < right_len ? left_len : right_len;
    const tda_Layout buf_layout = tda_elems_layout(s.elem, buf_len);
    void *buf = al ? tda_alloc(al, buf_layout) : nullptr;

    if (!buf) {
        merge_in_place(s, mid, cmp, ctx);
        return;
    }

    merge_buffered(s, mid, cmp, ctx, buf);
    tda_dealloc(al, buf, buf_layout);
}

/* ========== internals ========== */

/*
 * The buffer-free merge, by the standard recursion. Take the middle elem of the longer
 * run and binary search for its place in the other one. The two cuts split the problem
 * into two smaller merges whose results do not interleave. One rotate swaps the two
 * inner pieces, and each half is merged the same way.
 *
 * The choice of search keeps it stable. Cutting the left run searches the right with
 * lower_bound, so equal elems on the right land after; cutting the right run searches
 * the left with upper_bound, so equal elems on the left stay before.
 *
 * Each call halves the longer run, so the depth is logarithmic and plain recursion is
 * fine without an explicit stack.
 */
static void merge_in_place(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx) {
    const size_t left_len = mid, right_len = s.len - mid;

    if (left_len == 0 || right_len == 0) {
        return;
    }

    if (s.len == 2) {
        const tda_Span view = tda_span_mut_to_span(s);
        if (tda_right_goes_first(cmp, ctx, tda_span_get(view, 0), tda_span_get(view, 1))) {
            tda_span_swap_elems(s, 0, 1);
        }
        return;
    }

    const tda_Span view = tda_span_mut_to_span(s);
    size_t left_cut, right_cut;

    if (left_len > right_len) {
        left_cut = left_len / 2;
        const tda_Span right = tda_span_sub(view, mid, right_len);
        right_cut = mid + tda_span_lower_bound(right, tda_span_get(view, left_cut), cmp, ctx);
    } else {
        right_cut = mid + right_len / 2;
        const tda_Span left = tda_span_sub(view, 0, mid);
        left_cut = tda_span_upper_bound(left, tda_span_get(view, right_cut), cmp, ctx);
    }

    // swap the two inner pieces; new_mid is the boundary between the two sub-merges
    tda_span_rotate(tda_span_sub_mut(s, left_cut, right_cut - left_cut), mid - left_cut);
    const size_t new_mid = left_cut + (right_cut - mid);

    merge_in_place(tda_span_sub_mut(s, 0, new_mid), left_cut, cmp, ctx);
    merge_in_place(tda_span_sub_mut(s, new_mid, s.len - new_mid), right_cut - new_mid, cmp, ctx);
}


/*
 * The linear path. The shorter run goes into 'buf', and the merge runs over the span in
 * the direction where the write position cannot overtake the read one: forward when the
 * LEFT run was copied out, backward when it was the right one.
 *
 * Either way the loop stops once the buffer is empty, because the rest of the other run
 * is already in place and is not touched. Inside the loop the write and read positions
 * never coincide, so no elem is copied onto itself.
 */
static void merge_buffered(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx, void *buf) {
    const tda_Span view = tda_span_mut_to_span(s);

    if (mid <= s.len - mid) {
        memcpy(buf, tda_span_get(view, 0), mid * s.elem.size);
        merge_forward(s, mid, cmp, ctx, tda_span_from_data(buf, mid, s.elem));
    } else {
        memcpy(buf, tda_span_get(view, mid), (s.len - mid) * s.elem.size);
        merge_backward(s, mid, cmp, ctx, tda_span_from_data(buf, s.len - mid, s.elem));
    }
}

static void merge_forward(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx, tda_Span parked) {
    const tda_Span view = tda_span_mut_to_span(s);

    size_t write = 0;
    size_t parked_idx = 0, right_idx = mid;

    while (parked_idx < parked.len && right_idx < s.len) {
        const void *lhs = tda_span_get(parked, parked_idx), *rhs = tda_span_get(view, right_idx);

        // a tie keeps the buffered left run ahead of an equal elem on the right
        if (tda_right_goes_first(cmp, ctx, lhs, rhs)) {
            write = tda_emit(s, write, rhs);
            ++right_idx;
        } else {
            write = tda_emit(s, write, lhs);
            ++parked_idx;
        }
    }

    // the rest of the right run is already in place
    [[maybe_unused]] const size_t end = tda_emit_rest(s, write, parked, parked_idx);
    assert(right_idx < s.len || end == s.len);
}

static void merge_backward(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *ctx, tda_Span parked) {
    const tda_Span view = tda_span_mut_to_span(s);

    size_t write = s.len;
    size_t left_idx = mid, parked_idx = parked.len;

    while (left_idx > 0 && parked_idx > 0) {
        // filling from the back, the left elem is placed only when the right one goes
        // before it, so on a tie the buffered right elem lands later and the left run
        // stays first
        const bool take_left = tda_right_goes_first(cmp, ctx, tda_span_get(view, left_idx - 1), tda_span_get(parked, parked_idx - 1));

        --write;
        if (take_left) {
            --left_idx;
            memcpy(tda_span_get_mut(s, write), tda_span_get(view, left_idx), s.elem.size);
        } else {
            --parked_idx;
            memcpy(tda_span_get_mut(s, write), tda_span_get(parked, parked_idx), s.elem.size);
        }
    }

    // whatever is left of the buffer is the span's prefix: the left run ran out first
    assert(parked_idx == 0 || write == parked_idx);
    memcpy(tda_span_get_mut(s, 0), tda_span_get(parked, 0), parked_idx * s.elem.size);
}
