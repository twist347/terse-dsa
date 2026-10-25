#pragma once

#include "tda/core/align.h"
#include "tda/core/layout.h"
#include "tda/core/util.h"

#include <assert.h>
#include <stdckdint.h>
#include <stddef.h>
#include <stdint.h>

/* ========== byte-level pointer arithmetic ========== */

[[nodiscard]]
static inline const unsigned char *tda_byte_offset(const void *base, size_t stride, size_t n) {
    assert(base || n == 0);
    assert(stride > 0);

    return (const unsigned char *) base + stride * n;
}

[[nodiscard]]
static inline unsigned char *tda_byte_offset_mut(void *base, size_t stride, size_t n) {
    assert(base || n == 0);
    assert(stride > 0);

    return (unsigned char *) base + stride * n;
}

[[nodiscard]]
static inline ptrdiff_t tda_byte_diff(const void *a, const void *b) {
    assert(a);
    assert(b);

    return (const unsigned char *) a - (const unsigned char *) b;
}

/// whether 'ptr' lies in the 'bytes' bytes from 'base'; false for a null 'base'. Compared
/// as uintptr_t, since ordering pointers into different objects is undefined
[[nodiscard]]
static inline bool tda_addr_in(const void *base, size_t bytes, const void *ptr) {
    if (!base) {
        return false;
    }

    const uintptr_t pb = (uintptr_t) base;
    const uintptr_t pp = (uintptr_t) ptr;

    return pp >= pb && pp - pb < bytes;
}

/* ========== layouts ========== */

/// room for 'len' elems of 'elem' that already sit in memory somewhere, as a scratch copy
/// of a span; the size cannot overflow, since the elems already fit
[[nodiscard]]
static inline tda_Layout tda_elems_layout(tda_Layout elem, size_t len) {
    assert(tda_layout_is_type(elem));

    return (tda_Layout){.size = elem.size * len, .align = elem.align};
}

/* ========== bytes ========== */

static inline void tda_bytes_swap(void *a, void *b, size_t n) {
    assert(a);
    assert(b);

    unsigned char *pa = a, *pb = b;
    for (size_t i = 0; i < n; ++i) {
        TDA_SWAP(pa[i], pb[i]);
    }
}
