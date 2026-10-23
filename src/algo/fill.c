#include "tda/algo/fill.h"

#include <assert.h>
#include <string.h>

void tda_span_fill(tda_SpanMut s, const void *val) {
    TDA_SPAN_ASSERT(s);
    assert(val);

    for (size_t i = 0; i < s.len; ++i) {
        memcpy(tda_span_get_mut(s, i), val, s.elem.size);
    }
}

void tda_span_fill_zero(tda_SpanMut s) {
    TDA_SPAN_ASSERT(s);

    if (s.len == 0) {
        return;
    }

    memset(s.data, 0, s.len * s.elem.size);
}

void tda_span_generate(tda_SpanMut s, tda_Gen gen, void *ctx) {
    TDA_SPAN_ASSERT(s);
    assert(gen);

    for (size_t i = 0; i < s.len; ++i) {
        gen(tda_span_get_mut(s, i), i, ctx);
    }
}
