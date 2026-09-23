#include "tda/algo/search.h"

#include "tda/core/check.h"

#include <assert.h>

/* ========== private decls ========== */

/// whether 'sub' sits in 's' starting at 'at'. The caller guarantees the room
static bool matches_at(tda_Span s, tda_Span sub, size_t at, tda_Eq eq);

/* ========== find ========== */

bool tda_span_find(tda_Span s, const void *key, tda_Eq eq, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);
    assert(out_idx);

    for (size_t i = 0; i < s.len; ++i) {
        if (eq(tda_span_get(s, i), key)) {
            *out_idx = i;
            return true;
        }
    }
    return false;
}

bool tda_span_find_if(tda_Span s, tda_Pred pred, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);
    assert(out_idx);

    for (size_t i = 0; i < s.len; ++i) {
        if (pred(tda_span_get(s, i), ctx)) {
            *out_idx = i;
            return true;
        }
    }
    return false;
}

bool tda_span_find_sub(tda_Span s, tda_Span sub, tda_Eq eq, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    TDA_SPAN_ASSERT(sub);
    TDA_EXPECT(s.elem_size == sub.elem_size);
    assert(eq);
    assert(out_idx);

    if (sub.len == 0) {
        *out_idx = 0;
        return true;
    }
    if (sub.len > s.len) {
        return false;
    }

    // s.len - sub.len is the last start that still leaves room for the whole sub,
    // and cannot wrap: the case sub.len > s.len is already out
    for (size_t i = 0; i + sub.len <= s.len; ++i) {
        if (matches_at(s, sub, i, eq)) {
            *out_idx = i;
            return true;
        }
    }
    return false;
}

bool tda_span_find_sub_last(tda_Span s, tda_Span sub, tda_Eq eq, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    TDA_SPAN_ASSERT(sub);
    TDA_EXPECT(s.elem_size == sub.elem_size);
    assert(eq);
    assert(out_idx);

    if (sub.len == 0) {
        *out_idx = s.len;
        return true;
    }
    if (sub.len > s.len) {
        return false;
    }

    // counts down through 0, so the loop var is the start plus one — a size_t
    // running below zero wraps instead of ending the loop
    for (size_t start = s.len - sub.len + 1; start > 0; --start) {
        if (matches_at(s, sub, start - 1, eq)) {
            *out_idx = start - 1;
            return true;
        }
    }
    return false;
}

bool tda_span_find_run(tda_Span s, const void *key, size_t count, tda_Eq eq,
                       size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);
    assert(out_idx);

    if (count == 0) {
        *out_idx = 0;
        return true;
    }

    size_t run = 0;
    for (size_t i = 0; i < s.len; ++i) {
        run = eq(tda_span_get(s, i), key) ? run + 1 : 0;

        if (run == count) {
            *out_idx = i + 1 - count;
            return true;
        }
    }
    return false;
}

bool tda_span_find_any_of(tda_Span s, tda_Span set, tda_Eq eq, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    TDA_SPAN_ASSERT(set);
    TDA_EXPECT(s.elem_size == set.elem_size);
    assert(eq);
    assert(out_idx);

    for (size_t i = 0; i < s.len; ++i) {
        const void *elem = tda_span_get(s, i);

        for (size_t j = 0; j < set.len; ++j) {
            if (eq(elem, tda_span_get(set, j))) {
                *out_idx = i;
                return true;
            }
        }
    }
    return false;
}

bool tda_span_find_adjacent(tda_Span s, tda_Eq eq, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(eq);
    assert(out_idx);

    // starts at 1 so that an empty span has nothing to compare rather than
    // s.len - 1 wrapping around
    for (size_t i = 1; i < s.len; ++i) {
        if (eq(tda_span_get(s, i - 1), tda_span_get(s, i))) {
            *out_idx = i - 1;
            return true;
        }
    }
    return false;
}

bool tda_span_contains(tda_Span s, const void *key, tda_Eq eq) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);

    size_t idx;
    return tda_span_find(s, key, eq, &idx);
}

/* ========== count ========== */

size_t tda_span_count(tda_Span s, const void *key, tda_Eq eq) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);

    size_t count = 0;
    for (size_t i = 0; i < s.len; ++i) {
        if (eq(tda_span_get(s, i), key)) {
            ++count;
        }
    }
    return count;
}

size_t tda_span_count_if(tda_Span s, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);

    size_t count = 0;
    for (size_t i = 0; i < s.len; ++i) {
        if (pred(tda_span_get(s, i), ctx)) {
            ++count;
        }
    }
    return count;
}

/* ========== binary search ========== */

size_t tda_span_lower_bound(tda_Span s, const void *key, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(cmp);

    size_t lo = 0, hi = s.len;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const void *midp = tda_span_get(s, mid);

        if (cmp(midp, key) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

size_t tda_span_upper_bound(tda_Span s, const void *key, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(cmp);

    size_t lo = 0, hi = s.len;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const void *midp = tda_span_get(s, mid);

        if (cmp(midp, key) <= 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

bool tda_span_binary_search(tda_Span s, const void *key, tda_Cmp cmp, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(cmp);
    assert(out_idx);

    const size_t pos = tda_span_lower_bound(s, key, cmp);
    if (pos >= s.len) {
        return false;
    }

    const void *p = tda_span_get(s, pos);
    if (cmp(p, key) == 0) {
        *out_idx = pos;
        return true;
    }
    return false;
}

tda_Range tda_span_equal_range(tda_Span s, const void *key, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(cmp);

    const size_t lo = tda_span_lower_bound(s, key, cmp);

    const tda_Span tail = tda_span_sub(s, lo, s.len - lo);

    return (tda_Range){
        .lo = lo,
        .hi = lo + tda_span_upper_bound(tail, key, cmp)
    };
}

size_t tda_span_partition_point(tda_Span s, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);

    size_t lo = 0, hi = s.len;

    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;

        if (pred(tda_span_get(s, mid), ctx)) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

/* ========== predicates ========== */

bool tda_span_all_of(tda_Span s, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);

    for (size_t i = 0; i < s.len; ++i) {
        if (!pred(tda_span_get(s, i), ctx)) {
            return false;
        }
    }
    return true;
}

bool tda_span_any_of(tda_Span s, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);

    size_t idx;
    return tda_span_find_if(s, pred, ctx, &idx);
}

bool tda_span_none_of(tda_Span s, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);

    return !tda_span_any_of(s, pred, ctx);
}

/* ========== extremes ========== */

size_t tda_span_min_elem(tda_Span s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    size_t best = 0;
    const void *best_p = tda_span_get(s, 0);

    for (size_t i = 1; i < s.len; ++i) {
        const void *cur = tda_span_get(s, i);
        if (cmp(cur, best_p) < 0) {
            best = i;
            best_p = cur;
        }
    }

    return best;
}

size_t tda_span_max_elem(tda_Span s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    size_t best = 0;
    const void *best_p = tda_span_get(s, 0);

    for (size_t i = 1; i < s.len; ++i) {
        const void *cur = tda_span_get(s, i);
        if (cmp(cur, best_p) > 0) {
            best = i;
            best_p = cur;
        }
    }

    return best;
}

tda_MinMax tda_span_minmax_elem(tda_Span s, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    tda_MinMax out = {.min = 0, .max = 0};
    const void *min_p = tda_span_get(s, 0);
    const void *max_p = min_p;

    for (size_t i = 1; i < s.len; ++i) {
        const void *cur = tda_span_get(s, i);

        if (cmp(cur, min_p) < 0) {
            out.min = i;
            min_p = cur;
        }
        if (cmp(cur, max_p) > 0) {
            out.max = i;
            max_p = cur;
        }
    }

    return out;
}

/* ========== private defs ========== */

static bool matches_at(tda_Span s, tda_Span sub, size_t at, tda_Eq eq) {
    assert(at + sub.len <= s.len);

    for (size_t j = 0; j < sub.len; ++j) {
        if (!eq(tda_span_get(s, at + j), tda_span_get(sub, j))) {
            return false;
        }
    }
    return true;
}
