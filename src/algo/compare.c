#include "tda/algo/compare.h"

#include "tda/core/check.h"

#include <assert.h>
#include <string.h>

/* ========== internals ========== */

/// how many elems of 's' equal 'key' under 'eq'
[[nodiscard]]
static size_t count_of(tda_Span s, const void *key, tda_Eq eq);

/// whether some elem of 's' equals 'key' under 'eq'
[[nodiscard]]
static bool holds(tda_Span s, const void *key, tda_Eq eq);

/* ========== equality ========== */

bool tda_span_eq(tda_Span a, tda_Span b) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    TDA_EXPECT(a.elem_size == b.elem_size);

    if (a.len != b.len) {
        return false;
    }

    if (a.len == 0 || a.data == b.data) {
        return true;
    }

    return memcmp(a.data, b.data, a.len * a.elem_size) == 0;
}

bool tda_span_eq_by(tda_Span a, tda_Span b, tda_Eq eq) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    TDA_EXPECT(a.elem_size == b.elem_size);
    assert(eq);

    if (a.len != b.len) {
        return false;
    }

    if (a.len == 0 || a.data == b.data) {
        return true;
    }

    for (size_t i = 0; i < a.len; ++i) {
        const void *x = tda_span_get(a, i);
        const void *y = tda_span_get(b, i);
        if (!eq(x, y)) {
            return false;
        }
    }
    return true;
}

bool tda_span_mismatch(tda_Span a, tda_Span b, tda_Eq eq, size_t *out_idx) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(eq);
    assert(out_idx);
    TDA_EXPECT(a.elem_size == b.elem_size);

    if (a.data == b.data && a.len == b.len) {
        return false;
    }

    const size_t common = a.len < b.len ? a.len : b.len;

    for (size_t i = 0; i < common; ++i) {
        if (!eq(tda_span_get(a, i), tda_span_get(b, i))) {
            *out_idx = i;
            return true;
        }
    }
    return false;
}

bool tda_span_is_permutation(tda_Span a, tda_Span b, tda_Eq eq) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(eq);
    TDA_EXPECT(a.elem_size == b.elem_size);

    if (a.len != b.len) {
        return false;
    }

    // what the two share elem by elem is a permutation of itself, so only the rest is
    // counted; equal lengths and no mismatch at all is equal outright
    size_t from;
    if (!tda_span_mismatch(a, b, eq, &from)) {
        return true;
    }

    const tda_Span rest_a = tda_span_sub(a, from, a.len - from);
    const tda_Span rest_b = tda_span_sub(b, from, b.len - from);

    for (size_t i = 0; i < rest_a.len; ++i) {
        const void *elem = tda_span_get(rest_a, i);

        // an elem seen earlier in the rest was counted there, every copy at once
        if (holds(tda_span_sub(rest_a, 0, i), elem, eq)) {
            continue;
        }

        // its copies in 'a' all lie from 'i' on; 'b' has none to spare, so a zero there is
        // the cheap no
        const size_t in_b = count_of(rest_b, elem, eq);
        if (in_b == 0 || in_b != count_of(tda_span_sub(rest_a, i, rest_a.len - i), elem, eq)) {
            return false;
        }
    }

    return true;
}

/* ========== ordering ========== */

int tda_span_cmp(tda_Span a, tda_Span b, tda_Cmp cmp) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);
    assert(cmp);
    TDA_EXPECT(a.elem_size == b.elem_size);

    if (a.data == b.data && a.len == b.len) {
        return 0;
    }

    const size_t common = a.len < b.len ? a.len : b.len;

    for (size_t i = 0; i < common; ++i) {
        const int c = cmp(tda_span_get(a, i), tda_span_get(b, i));
        if (c != 0) {
            return c < 0 ? -1 : 1;
        }
    }
    return (a.len > b.len) - (a.len < b.len);
}

/* ========== internals ========== */

static size_t count_of(tda_Span s, const void *key, tda_Eq eq) {
    size_t n = 0;
    for (size_t i = 0; i < s.len; ++i) {
        n += eq(tda_span_get(s, i), key);
    }
    return n;
}

static bool holds(tda_Span s, const void *key, tda_Eq eq) {
    for (size_t i = 0; i < s.len; ++i) {
        if (eq(tda_span_get(s, i), key)) {
            return true;
        }
    }
    return false;
}
