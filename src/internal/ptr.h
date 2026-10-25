#pragma once

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

/* ========== alignment ========== */

/// room for 'len' elems of 'elem' that already sit in memory somewhere, as a scratch copy
/// of a span; the size cannot overflow, since the elems already fit
[[nodiscard]]
static inline tda_Layout tda_elems_layout(tda_Layout elem, size_t len) {
    assert(tda_layout_is_type(elem));

    return (tda_Layout){.size = elem.size * len, .align = elem.align};
}

[[nodiscard]]
static inline size_t tda_align_up(size_t val, size_t alignment) {
    assert(tda_layout_is_align(alignment));
    assert(val <= SIZE_MAX - (alignment - 1));

    return (val + (alignment - 1)) & ~(alignment - 1);
}

/// tda_align_up that reports overflow instead of asserting on it: true when 'val' rounded
/// up does not fit in size_t, and '*out' is written only when it does
[[nodiscard]]
static inline bool tda_ckd_align_up(size_t *out, size_t val, size_t alignment) {
    assert(out);
    assert(tda_layout_is_align(alignment));

    if (val > SIZE_MAX - (alignment - 1)) {
        return true;
    }
    *out = tda_align_up(val, alignment);

    return false;
}

/// how many bytes lie between 'ptr' and the first address at or after it aligned to
/// 'alignment'. A count and not a pointer: the caller can check it against a size before
/// forming an address that might lie past the end of its object
[[nodiscard]]
static inline size_t tda_ptr_align_pad(const void *ptr, size_t alignment) {
    assert(ptr);
    assert(tda_layout_is_align(alignment));

    return -(uintptr_t) ptr & (alignment - 1);
}

[[nodiscard]]
static inline bool tda_ptr_is_aligned(const void *ptr, size_t alignment) {
    assert(ptr);
    assert(tda_layout_is_align(alignment));

    return ((uintptr_t) ptr & (alignment - 1)) == 0;
}

static inline void tda_bytes_swap(void *a, void *b, size_t n) {
    assert(a);
    assert(b);

    unsigned char *pa = a;
    unsigned char *pb = b;
    for (size_t i = 0; i < n; ++i) {
        TDA_SWAP(pa[i], pb[i]);
    }
}
