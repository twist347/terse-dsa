#include "tda/algo/modify.h"

#include "internal/emit.h"
#include "internal/ptr.h"

#include <assert.h>

/* ========== internals ========== */

// lets the key forms reuse the predicate forms: the key, its equality and the
// equality's own ctx are passed as the predicate's ctx
typedef struct {
    const void *key;
    tda_Eq eq;
    void *ctx;
} KeyMatch;

[[nodiscard]]
static bool key_matches(const void *elem, void *ctx) {
    const KeyMatch *match = ctx;

    return match->eq(elem, match->key, match->ctx);
}

/* ========== unique ========== */

size_t tda_span_unique(tda_SpanMut s, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(eq);

    if (s.len < 2) {
        return s.len;
    }

    const tda_Span view = tda_span_mut_to_span(s);
    size_t write = 1;

    for (size_t read = 1; read < s.len; ++read) {
        const void *cur = tda_span_get(view, read);

        // compare against the last elem KEPT, not the previous one read: its
        // slot may already hold a different value
        if (!eq(tda_span_get(view, write - 1), cur, ctx)) {
            write = tda_emit_over(s, write, cur);
        }
    }

    return write;
}

/* ========== remove ========== */

size_t tda_span_remove(tda_SpanMut s, const void *key, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(eq);
    // the pass writes over the elems, so a key among them would change under it
    assert(!tda_addr_in(s.data, tda_span_bytes(tda_span_mut_to_span(s)), key));

    KeyMatch match = {.key = key, .eq = eq, .ctx = ctx};

    return tda_span_remove_if(s, key_matches, &match);
}

size_t tda_span_remove_if(tda_SpanMut s, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(pred);

    const tda_Span view = tda_span_mut_to_span(s);
    size_t write = 0;

    for (size_t read = 0; read < s.len; ++read) {
        const void *cur = tda_span_get(view, read);

        if (!pred(cur, ctx)) {
            write = tda_emit_over(s, write, cur);
        }
    }

    return write;
}

/* ========== replace ========== */

void tda_span_replace(tda_SpanMut s, const void *key, const void *val, tda_Eq eq, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(key);
    assert(val);
    assert(eq);
    // the first match would be overwritten, and with it the key
    assert(!tda_addr_in(s.data, tda_span_bytes(tda_span_mut_to_span(s)), key));

    KeyMatch match = {.key = key, .eq = eq, .ctx = ctx};

    tda_span_replace_if(s, val, key_matches, &match);
}

void tda_span_replace_if(tda_SpanMut s, const void *val, tda_Pred pred, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(val);
    assert(pred);

    const tda_Span view = tda_span_mut_to_span(s);

    for (size_t i = 0; i < s.len; ++i) {
        // tda_span_set: 'val' may be one of the elems
        if (pred(tda_span_get(view, i), ctx)) {
            tda_span_set(s, i, val);
        }
    }
}
