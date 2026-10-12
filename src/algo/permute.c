#include "tda/algo/permute.h"

#include "tda/algo/copy.h"
#include "tda/core/check.h"

#include "internal/ptr.h"

#include <assert.h>

/* ========== internals ========== */

/// whether v[i] comes strictly before v[j] in the walk order. Only "less than" is
/// asked, with the operands swapped for a descending walk, so a comparator that never
/// answers "greater" (a < b ? -1 : 0) still works
[[nodiscard]]
static bool goes_before(tda_Span v, tda_Cmp cmp, bool asc, size_t i, size_t j);

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

    // nothing to allocate for an empty span, so it works even on an exhausted
    // allocator
    if (s.len == 0) {
        *out_boundary = 0;
        return TDA_STATUS_OK;
    }

    const size_t bytes = s.len * s.elem_size;

    // room for the whole span, though only rejected elems go there: their count is
    // unknown until pred has seen every elem, and calling pred twice to count could
    // give different answers
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
            // kept never runs ahead of i, so this only overwrites elems already read
            if (kept != i) {
                tda_span_set(s, kept, elem);
            }
            ++kept;
        } else {
            tda_span_set(rejected, dropped, elem);
            ++dropped;
        }
    }

    // the kept elems are at the front in order; the rejected ones, also in order,
    // go after them
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

    // walking down, each step settles position i - 1 by drawing from [0, i), the elems
    // not placed yet. Drawing from the whole span each step is the classic bug: it
    // looks the same but skews the result.
    for (size_t i = s.len; i > 1; --i) {
        tda_span_swap_elems(s, i - 1, tda_rng_idx(rng, i));
    }
}

void tda_span_shuffle_prefix(tda_SpanMut s, size_t count, tda_Rng *rng) {
    TDA_SPAN_ASSERT(s);
    TDA_EXPECT(count <= s.len);
    assert(rng);

    // the same walk from the other end, so stopping early leaves the settled positions
    // at the front
    for (size_t i = 0; i < count; ++i) {
        tda_span_swap_elems(s, i, i + tda_rng_idx(rng, s.len - i));
    }
}

/* ========== internals ========== */

static bool goes_before(tda_Span v, tda_Cmp cmp, bool asc, size_t i, size_t j) {
    const void *lhs = tda_span_get(v, i);
    const void *rhs = tda_span_get(v, j);

    return asc ? cmp(lhs, rhs) < 0 : cmp(rhs, lhs) < 0;
}

static bool permute_step(tda_SpanMut s, tda_Cmp cmp, bool asc) {
    if (s.len < 2) {
        return false;
    }

    const tda_Span v = tda_span_mut_to_span(s);

    size_t pivot = s.len - 1;
    while (pivot > 0 && !goes_before(v, cmp, asc, pivot - 1, pivot)) {
        --pivot;
    }

    if (pivot == 0) {
        tda_span_reverse(s);
        return false;
    }
    --pivot;

    // this asks the question that stopped the loop above, so pivot + 1 answers it at the
    // latest; the bound guards against a comparator that answers differently twice
    size_t mate = s.len - 1;
    while (mate > pivot + 1 && !goes_before(v, cmp, asc, pivot, mate)) {
        --mate;
    }

    tda_span_swap_elems(s, pivot, mate);

    tda_span_reverse(tda_span_sub_mut(s, pivot + 1, s.len - pivot - 1));
    return true;
}
