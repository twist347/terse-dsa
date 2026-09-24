#include "tda/algo/merge.h"

#include "tda/algo/permute.h"
#include "tda/algo/search.h"
#include "tda/core/check.h"

#include "internal/emit.h"

#include <assert.h>
#include <string.h>

/* ========== internals ========== */

static void merge_in_place(tda_SpanMut s, size_t mid, tda_Cmp cmp);

/// merges over 's' with the shorter run parked in 'buf', in one linear pass
static void merge_buffered(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *buf);

/* ========== merge ========== */

void tda_span_merge(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(dst);
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(dst.elem_size == a.elem_size);
    TDA_EXPECT(dst.elem_size == b.elem_size);
    TDA_EXPECT(dst.len == a.len + b.len);

    size_t i = 0, j = 0;
    size_t out = 0;

    while (i < a.len && j < b.len) {
        const void *l = tda_span_get(a, i);
        const void *r = tda_span_get(b, j);

        // '<=' takes from 'a' on a tie, which keeps equal elems in the order they
        // arrived and is what makes tda_span_sort_stable stable through this
        if (cmp(l, r) <= 0) {
            out = tda_emit(dst, out, l);
            ++i;
        } else {
            out = tda_emit(dst, out, r);
            ++j;
        }
    }

    // only one of the two has anything left, but which one is not known here
    out = tda_emit_rest(dst, out, a, i);
    out = tda_emit_rest(dst, out, b, j);

    assert(out == dst.len);
}

/* ========== inplace merge ========== */

void tda_span_inplace_merge(tda_SpanMut s, size_t mid, tda_Cmp cmp, tda_Al *al) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(mid <= s.len);

    const size_t left_len = mid;
    const size_t right_len = s.len - mid;

    if (left_len == 0 || right_len == 0) {
        return;
    }

    // the buffer holds the shorter run, so it is never more than half the span
    const size_t buf_len = left_len < right_len ? left_len : right_len;
    void *buf = al ? tda_alloc(al, buf_len * s.elem_size) : nullptr;

    if (!buf) {
        merge_in_place(s, mid, cmp);
        return;
    }

    merge_buffered(s, mid, cmp, buf);
    tda_dealloc(al, buf, buf_len * s.elem_size);
}

/* ========== internals ========== */

/*
 * The buffer-free merge, by the standard recursion. Take the middle elem of the longer
 * run and binary search for its place in the other one; that pair of cuts splits the
 * problem into two smaller merges whose results do not interleave. One rotate brings the
 * two inner pieces past each other, and the halves are merged the same way.
 *
 * Which run gets cut in the middle is what keeps it stable: cutting the left one searches
 * the right with lower_bound, so equal elems on the right land after, and cutting the
 * right one searches the left with upper_bound, so equal elems on the left stay before.
 *
 * Depth is logarithmic — each call halves the longer run — so the recursion needs no
 * unrolling into an explicit stack.
 */
static void merge_in_place(tda_SpanMut s, size_t mid, tda_Cmp cmp) {
    const size_t left_len = mid;
    const size_t right_len = s.len - mid;

    if (left_len == 0 || right_len == 0) {
        return;
    }

    if (s.len == 2) {
        const tda_Span cs = tda_span_mut_to_span(s);
        if (cmp(tda_span_get(cs, 1), tda_span_get(cs, 0)) < 0) {
            tda_span_swap_elems(s, 0, 1);
        }
        return;
    }

    const tda_Span cs = tda_span_mut_to_span(s);
    size_t left_cut;
    size_t right_cut;

    if (left_len > right_len) {
        left_cut = left_len / 2;
        right_cut =
                mid + tda_span_lower_bound(
                    tda_span_sub(cs, mid, right_len), tda_span_get(cs, left_cut), cmp
                );
    } else {
        right_cut = mid + right_len / 2;
        left_cut = tda_span_upper_bound(
            tda_span_sub(cs, 0, mid), tda_span_get(cs, right_cut), cmp
        );
    }

    // the two inner pieces trade places, and the boundary between what is already
    // settled on the left and what is settled on the right lands here
    tda_span_rotate(tda_span_sub_mut(s, left_cut, right_cut - left_cut), mid - left_cut);
    const size_t new_mid = left_cut + (right_cut - mid);

    merge_in_place(tda_span_sub_mut(s, 0, new_mid), left_cut, cmp);
    merge_in_place(tda_span_sub_mut(s, new_mid, s.len - new_mid), right_cut - new_mid, cmp);
}


/*
 * The linear path. Whichever run is shorter goes into 'buf', and the merge then runs over
 * the span itself in the direction that keeps the write position from overtaking the read
 * one: forward when the LEFT run was parked, backward when it was the right one.
 *
 * Either way the loop stops as soon as the buffer runs dry, because what is left of the
 * other run is already sitting where it belongs — the tail of the span is not touched at
 * all. Inside the loop the write position and the read position are never equal, so no
 * elem is ever copied onto itself.
 */
static void merge_buffered(tda_SpanMut s, size_t mid, tda_Cmp cmp, void *buf) {
    const size_t tsz = s.elem_size;
    const size_t left_len = mid;
    const size_t right_len = s.len - mid;
    const tda_Span cs = tda_span_mut_to_span(s);

    if (left_len <= right_len) {
        memcpy(buf, tda_span_get(cs, 0), left_len * tsz);
        const tda_Span parked = tda_span_from_data(buf, left_len, tsz);

        size_t w = 0;
        size_t b = 0;
        size_t r = mid;

        while (b < left_len && r < s.len) {
            // '<' keeps the parked left run ahead of an equal elem on the right
            const bool take_right = cmp(tda_span_get(cs, r), tda_span_get(parked, b)) < 0;

            memcpy(tda_span_get_mut(s, w), take_right ? tda_span_get(cs, r) : tda_span_get(parked, b), tsz);
            take_right ? ++r : ++b;
            ++w;
        }

        while (b < left_len) {
            memcpy(tda_span_get_mut(s, w), tda_span_get(parked, b), tsz);
            ++b;
            ++w;
        }

        return;
    }

    memcpy(buf, tda_span_get(cs, mid), right_len * tsz);
    const tda_Span parked = tda_span_from_data(buf, right_len, tsz);

    size_t w = s.len;
    size_t l = mid;
    size_t b = right_len;

    while (l > 0 && b > 0) {
        // '>' takes the left elem only when it is strictly greater, so on a tie the
        // parked right elem is placed later and the left run keeps its lead
        const bool take_left = cmp(tda_span_get(cs, l - 1), tda_span_get(parked, b - 1)) > 0;

        --w;
        if (take_left) {
            --l;
            memcpy(tda_span_get_mut(s, w), tda_span_get(cs, l), tsz);
        } else {
            --b;
            memcpy(tda_span_get_mut(s, w), tda_span_get(parked, b), tsz);
        }
    }

    while (b > 0) {
        --b;
        --w;
        memcpy(tda_span_get_mut(s, w), tda_span_get(parked, b), tsz);
    }
}
