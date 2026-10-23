#include "tda/algo/search.h"

#include "tda/core/check.h"

#include <assert.h>

/* ========== internals ========== */

/// whether 'sub' occurs in 's' at 'at'; the caller guarantees it fits
static bool matches_at(tda_Span s, tda_Span sub, size_t idx, tda_Eq eq, void *ctx);

/// the first index whose elem orders after 'key' ('upper') or not before it (otherwise)
[[nodiscard]]
static size_t bound(tda_Span s, const void *key, tda_Cmp cmp, void *ctx, bool upper);

/// whether 'cur' takes the place of 'best' as the min, or as the max when 'max' is set.
/// Only a strictly better elem does, so among equals the first one stays: the tie rule
/// of min_elem, max_elem and minmax_elem alike
[[nodiscard]]
static bool beats(tda_Cmp cmp, void *ctx, const void *cur, const void *best, bool max);

/// the min_elem and max_elem walk
[[nodiscard]]
static size_t extreme(tda_Span s, tda_Cmp cmp, void *ctx, bool max);

/* ========== find ========== */

bool tda_span_find(tda_Span s, const void *key, tda_Eq eq, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);
    assert(out_idx);

    for (size_t i = 0; i < s.len; ++i) {
        if (eq(tda_span_get(s, i), key, ctx)) {
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

bool tda_span_find_last(tda_Span s, const void *key, tda_Eq eq, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);
    assert(out_idx);

    // counts down through 0, so the loop var is the index plus one
    for (size_t i = s.len; i > 0; --i) {
        if (eq(tda_span_get(s, i - 1), key, ctx)) {
            *out_idx = i - 1;
            return true;
        }
    }

    return false;
}

bool tda_span_find_last_if(tda_Span s, tda_Pred pred, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);
    assert(out_idx);

    for (size_t i = s.len; i > 0; --i) {
        if (pred(tda_span_get(s, i - 1), ctx)) {
            *out_idx = i - 1;
            return true;
        }
    }

    return false;
}

bool tda_span_find_sub(tda_Span s, tda_Span sub, tda_Eq eq, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    TDA_SPAN_ASSERT(sub);
    TDA_EXPECT(tda_layout_eq(s.elem, sub.elem));
    assert(eq);
    assert(out_idx);

    if (sub.len == 0) {
        *out_idx = 0;
        return true;
    }
    if (sub.len > s.len) {
        return false;
    }

    // s.len - sub.len is the last start with room for all of 'sub'; it cannot wrap,
    // since sub.len > s.len was handled above
    for (size_t i = 0; i + sub.len <= s.len; ++i) {
        if (matches_at(s, sub, i, eq, ctx)) {
            *out_idx = i;
            return true;
        }
    }

    return false;
}

bool tda_span_find_last_sub(tda_Span s, tda_Span sub, tda_Eq eq, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    TDA_SPAN_ASSERT(sub);
    TDA_EXPECT(tda_layout_eq(s.elem, sub.elem));
    assert(eq);
    assert(out_idx);

    if (sub.len == 0) {
        *out_idx = s.len;
        return true;
    }
    if (sub.len > s.len) {
        return false;
    }

    // counts down through 0, so the loop var is the start plus one; a size_t going
    // below zero would wrap instead of ending the loop
    for (size_t start = s.len - sub.len + 1; start > 0; --start) {
        if (matches_at(s, sub, start - 1, eq, ctx)) {
            *out_idx = start - 1;
            return true;
        }
    }

    return false;
}

bool tda_span_find_run(tda_Span s, const void *key, size_t count, tda_Eq eq, void *ctx,
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
        run = eq(tda_span_get(s, i), key, ctx) ? run + 1 : 0;

        if (run == count) {
            *out_idx = i + 1 - count;
            return true;
        }
    }

    return false;
}

bool tda_span_find_any_of(tda_Span s, tda_Span set, tda_Eq eq, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    TDA_SPAN_ASSERT(set);
    TDA_EXPECT(tda_layout_eq(s.elem, set.elem));
    assert(eq);
    assert(out_idx);

    for (size_t i = 0; i < s.len; ++i) {
        const void *elem = tda_span_get(s, i);

        for (size_t j = 0; j < set.len; ++j) {
            if (eq(elem, tda_span_get(set, j), ctx)) {
                *out_idx = i;
                return true;
            }
        }
    }

    return false;
}

bool tda_span_find_adjacent(tda_Span s, tda_Eq eq, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(eq);
    assert(out_idx);

    // starts at 1 so an empty span compares nothing instead of s.len - 1 wrapping
    for (size_t i = 1; i < s.len; ++i) {
        if (eq(tda_span_get(s, i - 1), tda_span_get(s, i), ctx)) {
            *out_idx = i - 1;
            return true;
        }
    }

    return false;
}

bool tda_span_contains(tda_Span s, const void *key, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);

    size_t idx;

    return tda_span_find(s, key, eq, ctx, &idx);
}

/* ========== count ========== */

size_t tda_span_count(tda_Span s, const void *key, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);

    size_t count = 0;
    for (size_t i = 0; i < s.len; ++i) {
        if (eq(tda_span_get(s, i), key, ctx)) {
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

size_t tda_span_lower_bound(tda_Span s, const void *key, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(cmp);

    return bound(s, key, cmp, ctx, false);
}

size_t tda_span_upper_bound(tda_Span s, const void *key, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(cmp);

    return bound(s, key, cmp, ctx, true);
}

bool tda_span_binary_search(tda_Span s, const void *key, tda_Cmp cmp, void *ctx, size_t *out_idx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(cmp);
    assert(out_idx);

    const size_t pos = tda_span_lower_bound(s, key, cmp, ctx);
    if (pos >= s.len) {
        return false;
    }

    // the elem at pos is not less than 'key'; it is equal unless 'key' is less than it
    const void *elem = tda_span_get(s, pos);
    if (!(cmp(key, elem, ctx) < 0)) {
        *out_idx = pos;
        return true;
    }

    return false;
}

tda_Range tda_span_equal_range(tda_Span s, const void *key, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(cmp);

    const size_t lo = tda_span_lower_bound(s, key, cmp, ctx);

    const tda_Span tail = tda_span_sub(s, lo, s.len - lo);

    return (tda_Range){
        .lo = lo,
        .hi = lo + tda_span_upper_bound(tail, key, cmp, ctx)
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

bool tda_span_starts_with(tda_Span s, tda_Span prefix, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(s);
    TDA_SPAN_ASSERT(prefix);
    TDA_EXPECT(tda_layout_eq(s.elem, prefix.elem));
    assert(eq);

    return prefix.len <= s.len && matches_at(s, prefix, 0, eq, ctx);
}

bool tda_span_ends_with(tda_Span s, tda_Span suffix, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(s);
    TDA_SPAN_ASSERT(suffix);
    TDA_EXPECT(tda_layout_eq(s.elem, suffix.elem));
    assert(eq);

    return suffix.len <= s.len && matches_at(s, suffix, s.len - suffix.len, eq, ctx);
}

/* ========== extremes ========== */

size_t tda_span_min_elem(tda_Span s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    return extreme(s, cmp, ctx, false);
}

size_t tda_span_max_elem(tda_Span s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    return extreme(s, cmp, ctx, true);
}

tda_MinMax tda_span_minmax_elem(tda_Span s, tda_Cmp cmp, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(cmp);
    TDA_EXPECT(s.len > 0);

    tda_MinMax out = {.min = 0, .max = 0};
    const void *min_p = tda_span_get(s, 0);
    const void *max_p = min_p;

    for (size_t i = 1; i < s.len; ++i) {
        const void *cur = tda_span_get(s, i);

        if (beats(cmp, ctx, cur, min_p, false)) {
            out.min = i;
            min_p = cur;
        }
        if (beats(cmp, ctx, cur, max_p, true)) {
            out.max = i;
            max_p = cur;
        }
    }

    return out;
}

/* ========== internals ========== */

static size_t bound(tda_Span s, const void *key, tda_Cmp cmp, void *ctx, bool upper) {
    size_t lo = 0, hi = s.len;

    // asked only as "less", one way round for each bound, so a comparator that never
    // answers "greater" works too: the lower bound passes every elem less than 'key',
    // the upper every elem 'key' is not less than
    while (lo < hi) {
        const size_t mid = lo + (hi - lo) / 2;
        const void *elem = tda_span_get(s, mid);
        const bool before = upper ? !(cmp(key, elem, ctx) < 0) : cmp(elem, key, ctx) < 0;

        if (before) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }

    return lo;
}

static bool beats(tda_Cmp cmp, void *ctx, const void *cur, const void *best, bool max) {
    // asked as "less" both ways round, so the max needs no "greater" from 'cmp'
    return max ? cmp(best, cur, ctx) < 0 : cmp(cur, best, ctx) < 0;
}

static size_t extreme(tda_Span s, tda_Cmp cmp, void *ctx, bool max) {
    size_t best = 0;
    const void *best_p = tda_span_get(s, 0);

    for (size_t i = 1; i < s.len; ++i) {
        const void *cur = tda_span_get(s, i);
        if (beats(cmp, ctx, cur, best_p, max)) {
            best = i;
            best_p = cur;
        }
    }

    return best;
}

static bool matches_at(tda_Span s, tda_Span sub, size_t idx, tda_Eq eq, void *ctx) {
    assert(idx + sub.len <= s.len);

    for (size_t j = 0; j < sub.len; ++j) {
        if (!eq(tda_span_get(s, idx + j), tda_span_get(sub, j), ctx)) {
            return false;
        }
    }

    return true;
}
