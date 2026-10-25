#pragma once

#include "tda/core/layout.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/// @file

/// @defgroup core_align core/align
/// @ingroup core
/// @brief rounding sizes and addresses up to an alignment
///
/// The arithmetic an allocator of one's own needs to honour the alignment every tda_Al
/// request carries: where the next block may start in a buffer, and how big a size gets
/// once rounded up. Every alignment is a power of two, as tda_layout_is_align checks.
///
/// Header-only: every function is static inline.
///
/// @par Example
/// @snippet core/example_align.c bump_def
/// @snippet core/example_align.c bump
/// @{

/// @name sizes
/// @{

/// 'val' rounded up to the next multiple of 'align'
/// @param val the size; rounding it up must fit in size_t
/// @param align a power of two
/// @return the smallest multiple of 'align' not below 'val'
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_align_up(size_t val, size_t align) {
    assert(tda_layout_is_align(align));
    assert(val <= SIZE_MAX - (align - 1));

    return (val + (align - 1)) & ~(align - 1);
}

/// tda_align_up that reports an overflow instead of asserting on it, in the shape of
/// C23's ckd_add
/// @param[out] out 'val' rounded up, written only when it fits
/// @param val the size
/// @param align a power of two
/// @return true when the rounded size does not fit in size_t
/// @bigo{1}
[[nodiscard]]
static inline bool tda_align_ckd_up(size_t *out, size_t val, size_t align) {
    assert(out);
    assert(tda_layout_is_align(align));

    if (val > SIZE_MAX - (align - 1)) {
        return true;
    }
    *out = tda_align_up(val, align);

    return false;
}

/// @}

/// @name addresses
/// @{

/// how many bytes lie between 'ptr' and the first address at or after it on 'align'. A
/// count and not a pointer: the caller checks it against the room left before forming an
/// address that might lie past the end of its buffer
/// @param ptr any address
/// @param align a power of two
/// @return 0 when 'ptr' is already on 'align', otherwise less than 'align'
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_align_pad(const void *ptr, size_t align) {
    assert(ptr);
    assert(tda_layout_is_align(align));

    return -(uintptr_t) ptr & (align - 1);
}

/// whether 'ptr' sits on 'align', as an allocator's block must sit on the alignment it
/// was asked for
/// @param ptr any address
/// @param align a power of two
/// @return whether the address is a multiple of 'align'
/// @bigo{1}
[[nodiscard]]
static inline bool tda_align_meets(const void *ptr, size_t align) {
    assert(ptr);
    assert(tda_layout_is_align(align));

    return ((uintptr_t) ptr & (align - 1)) == 0;
}

/// @}

/// @}
