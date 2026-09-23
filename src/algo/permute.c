#include "tda/algo/permute.h"

#include "tda/algo/copy.h"
#include "tda/core/check.h"

#include "internal/ptr.h"

#include <assert.h>

/* ========== internals ========== */

[[nodiscard]]
static bool permute_step(tda_SpanMut s, tda_Cmp cmp, bool asc);

/* ========== permute ========== */

void tda_span_reverse(tda_SpanMut s) {
    TDA_SPAN_ASSERT(s);

    if (s.len < 2) {
        return;
    }

    size_t left = 0, right = s.len - 1;

    while (left < right) {
        tda_span_swap_elems(s, left, right);
        ++left;
        --right;
    }
}

void tda_span_rotate(tda_SpanMut s, size_t mid) {
    TDA_SPAN_ASSERT(s);
    TDA_EXPECT(mid <= s.len);

    if (mid == 0 || mid == s.len) {
        return;
    }

    const tda_SpanMut left = tda_span_sub_mut(s, 0, mid);
    tda_span_reverse(left);

    const tda_SpanMut right = tda_span_sub_mut(s, mid, s.len - mid);
    tda_span_reverse(right);

    tda_span_reverse(s);
}

void tda_span_swap_ranges(tda_SpanMut a, tda_SpanMut b) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    TDA_EXPECT(a.elem_size == b.elem_size);
    TDA_EXPECT(a.len == b.len);

    if (a.len == 0 || a.data == b.data) {
        return;
    }

    for (size_t i = 0; i < a.len; ++i) {
        tda_bytes_swap(tda_span_get_mut(a, i), tda_span_get_mut(b, i), a.elem_size);
    }
}

bool tda_span_next_permutation(tda_SpanMut s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    return permute_step(s, cmp, true);
}

bool tda_span_prev_permutation(tda_SpanMut s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);

    return permute_step(s, cmp, false);
}

size_t tda_span_partition(tda_SpanMut s, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);

    const tda_Span view = tda_span_mut_to_span(s);
    size_t boundary = 0;

    for (size_t i = 0; i < s.len; ++i) {
        if (pred(tda_span_get(view, i), ctx)) {
            if (i != boundary) {
                tda_span_swap_elems(s, i, boundary);
            }
            ++boundary;
        }
    }
    return boundary;
}

tda_Status tda_span_partition_stable(
    tda_SpanMut s,
    tda_Pred pred,
    void *ctx,
    tda_Al *al,
    size_t *out_boundary
) {
    TDA_SPAN_ASSERT(s);
    assert(pred);
    assert(al);
    assert(out_boundary);

    // nothing to move and nothing to ask, so nothing to allocate either: an
    // allocator with no room left must still be able to partition an empty span
    if (s.len == 0) {
        *out_boundary = 0;
        return TDA_STATUS_OK;
    }

    const size_t bytes = s.len * s.elem_size;

    // room for the whole span, though only the rejected elems are ever put there:
    // how many those are is not known before pred has seen them all, and asking it
    // twice to find out would be a second, differently timed set of answers
    void *buf = tda_alloc(al, bytes);
    if (!buf) {
        return TDA_STATUS_ERR_NO_MEM;
    }

    const tda_Span view = tda_span_mut_to_span(s);
    const tda_SpanMut rejected = tda_span_from_data_mut(buf, s.len, s.elem_size);

    size_t kept = 0, dropped = 0;

    for (size_t i = 0; i < s.len; ++i) {
        const void *elem = tda_span_get(view, i);

        if (pred(elem, ctx)) {
            // kept never runs ahead of i, so this only ever overwrites an elem
            // that has already been read
            if (kept != i) {
                tda_span_set(s, kept, elem);
            }
            ++kept;
        } else {
            tda_span_set(rejected, dropped, elem);
            ++dropped;
        }
    }

    // the front holds the kept ones in order, the rest of the span is free for the
    // rejected ones — also in order, since they were appended as they were met
    tda_span_copy(
        tda_span_sub_mut(s, kept, dropped),
        tda_span_sub(tda_span_mut_to_span(rejected), 0, dropped)
    );

    tda_dealloc(al, buf, bytes);

    *out_boundary = kept;
    return TDA_STATUS_OK;
}

bool tda_span_is_partitioned(tda_Span s, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);

    size_t i = 0;
    while (i < s.len && pred(tda_span_get(s, i), ctx)) {
        ++i;
    }
    while (i < s.len && !pred(tda_span_get(s, i), ctx)) {
        ++i;
    }

    return i == s.len;
}

void tda_span_shuffle(tda_SpanMut s, tda_Rng *rng) {
    TDA_SPAN_ASSERT(s);
    assert(rng);

    // walking down: each step settles position i - 1 by drawing from [0, i), the elems
    // not placed yet. Drawing from the whole span every step instead is the classic bug
    // — it looks the same and skews the result.
    for (size_t i = s.len; i > 1; --i) {
        tda_span_swap_elems(s, i - 1, tda_rng_idx(rng, i));
    }
}

void tda_span_shuffle_prefix(tda_SpanMut s, size_t count, tda_Rng *rng) {
    TDA_SPAN_ASSERT(s);
    TDA_EXPECT(count <= s.len);
    assert(rng);

    // the same walk from the other end, so stopping early leaves the settled positions
    // at the front rather than the back
    for (size_t i = 0; i < count; ++i) {
        tda_span_swap_elems(s, i, i + tda_rng_idx(rng, s.len - i));
    }
}

/* ========== internals ========== */

static bool permute_step(tda_SpanMut s, tda_Cmp cmp, bool asc) {
    if (s.len < 2) {
        return false;
    }

    const tda_Span v = tda_span_mut_to_span(s);

    size_t pivot = s.len - 1;
    while (pivot > 0) {
        const int c = cmp(tda_span_get(v, pivot - 1), tda_span_get(v, pivot));
        if (asc ? c < 0 : c > 0) {
            break;
        }
        --pivot;
    }

    if (pivot == 0) {
        tda_span_reverse(s);
        return false;
    }
    --pivot;

    size_t mate = s.len - 1;
    while (true) {
        const int c = cmp(tda_span_get(v, mate), tda_span_get(v, pivot));
        if (asc ? c > 0 : c < 0) {
            break;
        }
        --mate;
    }

    tda_span_swap_elems(s, pivot, mate);

    tda_span_reverse(tda_span_sub_mut(s, pivot + 1, s.len - pivot - 1));
    return true;
}
