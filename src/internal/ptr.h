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

[[nodiscard]]
static inline size_t tda_ptr_distance(const void *a, const void *b, size_t stride) {
    assert(a);
    assert(b);
    assert(stride > 0);

    const ptrdiff_t diff = tda_byte_diff(a, b);
    assert(diff >= 0);
    assert((size_t) diff % stride == 0);

    return (size_t) diff / stride;
}

/* ========== alignment ========== */

/// malloc's own alignment, alignof(max_align_t): what the plain libc calls are owed for a
/// block at least that long, and what the header at the front of a caller's buffer is
/// put on
static constexpr size_t TDA_DEFAULT_ALIGNMENT = alignof(max_align_t);

/// room for 'len' elems of 'elem' that already sit in memory somewhere, as a scratch copy
/// of a span; the size cannot overflow, since the elems already fit
[[nodiscard]]
static inline tda_Layout tda_elems_layout(tda_Layout elem, size_t len) {
    assert(tda_layout_is_type(elem));

    return (tda_Layout){.size = elem.size * len, .align = elem.align};
}

[[nodiscard]]
static inline size_t tda_align_up(size_t val, size_t alignment) {
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0);
    assert(val <= SIZE_MAX - (alignment - 1));

    return (val + (alignment - 1)) & ~(alignment - 1);
}

/// tda_align_up that reports overflow instead of asserting on it: true when 'val' rounded
/// up does not fit in size_t, and '*out' is written only when it does
[[nodiscard]]
static inline bool tda_ckd_align_up(size_t *out, size_t val, size_t alignment) {
    assert(out);
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0);

    if (val > SIZE_MAX - (alignment - 1)) {
        return true;
    }
    *out = tda_align_up(val, alignment);

    return false;
}

[[nodiscard]]
static inline size_t tda_align_down(size_t val, size_t alignment) {
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0);

    return val & ~(alignment - 1);
}

/// the first address at or after 'ptr' that is aligned to 'alignment'. The mask is built
/// in uintptr_t: spelled over size_t it would be a narrower type wherever the two differ,
/// and the address would be cut down instead of rounded up
[[nodiscard]]
static inline void *tda_ptr_align_up(void *ptr, size_t alignment) {
    assert(ptr);
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0);

    const uintptr_t addr = (uintptr_t) ptr;
    assert(addr <= UINTPTR_MAX - (alignment - 1));

    return (void *) ((addr + ((uintptr_t) alignment - 1)) & ~((uintptr_t) alignment - 1));
}

/// how many bytes lie between 'ptr' and the first address at or after it aligned to
/// 'alignment'. A count and not a pointer: the caller can check it against a size before
/// forming an address that might lie past the end of its object
[[nodiscard]]
static inline size_t tda_ptr_align_pad(const void *ptr, size_t alignment) {
    assert(ptr);
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0);

    return -(uintptr_t) ptr & (alignment - 1);
}

[[nodiscard]]
static inline bool tda_ptr_is_aligned(const void *ptr, size_t alignment) {
    assert(ptr);
    assert(alignment > 0);
    assert((alignment & (alignment - 1)) == 0);

    return ((uintptr_t) ptr & (alignment - 1)) == 0;
}

/// carves a caller's buffer into a header of 'head_size' and the rest, both at
/// TDA_DEFAULT_ALIGNMENT. Offsets are checked against 'size' before an address is formed
/// @return false when the header does not fit; the outs are written only on true
[[nodiscard]]
static inline bool tda_buf_split_head(
    void *buf,
    size_t size,
    size_t head_size,
    void **out_head,
    void **out_rest,
    size_t *out_rest_size
) {
    assert(buf);
    assert(out_head);
    assert(out_rest);
    assert(out_rest_size);

    const size_t head_offset = tda_ptr_align_pad(buf, TDA_DEFAULT_ALIGNMENT);
    size_t rest_offset;
    if (ckd_add(&rest_offset, head_offset, tda_align_up(head_size, TDA_DEFAULT_ALIGNMENT))
        || rest_offset > size) {
        return false;
    }

    *out_head = tda_byte_offset_mut(buf, 1, head_offset);
    *out_rest = tda_byte_offset_mut(buf, 1, rest_offset);
    *out_rest_size = size - rest_offset;
    return true;
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
