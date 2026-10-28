#include "tda/algo/fill.h"

#include "internal/ptr.h"

#include <assert.h>
#include <string.h>

void tda_span_fill(tda_SpanMut s, const void *val) {
    TDA_SPAN_ASSERT(s);
    assert(val);

    if (s.len == 0) {
        return;
    }

    // tda_span_set: 'val' may be one of the elems, and only the first write reads it.
    // Then the filled prefix doubles, copied onto the room right after it
    tda_span_set(s, 0, val);

    const size_t size = s.elem.size;
    for (size_t done = 1; done < s.len;) {
        const size_t n = done < s.len - done ? done : s.len - done;
        memcpy(tda_byte_offset_mut(s.data, size, done), s.data, n * size);
        done += n;
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
