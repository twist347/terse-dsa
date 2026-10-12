#pragma once

#include "tda/core/export.h"

#include <stdckdint.h>
#include <stddef.h>

/// @file

/// @defgroup alloc_alloc alloc/alloc
/// @ingroup alloc
/// @brief tda_Al — the allocator interface every owning type is built on
///
/// An interface to dispatch through, so the ops are named for the verb and take the
/// allocator first, as fprintf takes its FILE: tda_alloc(al, n). Only ops on the
/// allocator itself carry a slug, as tda_al_default does.
///
/// The wrappers return the pointer and signal failure with null. This is the one place a
/// fallible op returns no tda_Status; [[nodiscard]] on the pointer catches the same
/// dropped check. Null is not always failure: a request for 0 bytes gives null, and each
/// wrapper says what its nulls mean.
///
/// A table must fill in alloc and dealloc and may leave calloc and realloc null; the
/// wrappers then build them from the other two. The aligned allocator relies on this for
/// both, the arena and the pool for calloc. An allocator over another borrows it: the
/// parent outlives the child, and they drop in reverse order of building.
///
/// @par Example
/// @snippet alloc/example_alloc.c use
/// @snippet alloc/example_alloc.c custom
/// @snippet alloc/example_alloc.c wrap
/// @{

/// The operations and the state they run on, passed around by pointer.
typedef struct {
    void *ctx; ///< implementation state, passed to every callback

    /// 'size' bytes, uninitialized, or null. Required.
    void *(*alloc)(void *ctx, size_t size);

    /// 'num' * 'size' zeroed bytes, or null. Optional: null means alloc and memset.
    void *(*calloc)(void *ctx, size_t num, size_t size);

    /// 'ptr' resized from 'old_size' to 'new_size' bytes, or null. Optional: if null,
    /// alloc, copy and dealloc are used.
    void *(*realloc)(void *ctx, void *ptr, size_t old_size, size_t new_size);

    /// gives back 'size' bytes at 'ptr'. Required.
    void (*dealloc)(void *ctx, void *ptr, size_t size);
} tda_Al;

/// @name wrappers
/// @{

/// 'size' bytes, uninitialized
/// @param al the allocator
/// @param size how many bytes; 0 gives null and is not an error
/// @return the block, or null; for a non-zero 'size', null means out of memory
[[nodiscard]] TDA_API
void *tda_alloc(tda_Al *al, size_t size);

/// 'num' * 'size' bytes, zeroed
/// @param al the allocator
/// @param num how many elems
/// @param size the size of one
/// @return the block, or null when either operand is 0, the product overflows size_t, or
///         the allocation fails
[[nodiscard]] TDA_API
void *tda_calloc(tda_Al *al, size_t num, size_t size);

/// the block at 'ptr', resized to 'new_size' bytes
/// @param al the allocator
/// @param ptr the block to resize; may be null only when 'old_size' is 0
/// @param old_size what 'ptr' was allocated as
/// @param new_size what it should become; 0 frees 'ptr' and returns null, not a failure
/// @return the block, which may have moved, or null on failure with 'ptr' untouched
/// @warning assign to a temporary, not over 'ptr': on failure the old block is still
///          yours, and overwriting its only pointer leaks it
[[nodiscard]] TDA_API
void *tda_realloc(tda_Al *al, void *ptr, size_t old_size, size_t new_size);

/// gives the block back
/// @param al the allocator; must be the one 'ptr' came from
/// @param ptr the block; null is a no-op
/// @param size what 'ptr' was allocated as; must match, since an allocator may rely on it
TDA_API
void tda_dealloc(tda_Al *al, void *ptr, size_t size);

/// @}

/// @name macros
/// @{

/// @cond
// what TDA_ALLOC and TDA_REALLOC expand to; a function evaluates each argument once,
// while a macro checking the product would evaluate the count twice

[[nodiscard]]
static inline void *tda_alloc_n_(tda_Al *al, size_t count, size_t size) {
    size_t bytes;
    if (ckd_mul(&bytes, count, size)) {
        return nullptr;
    }

    return tda_alloc(al, bytes);
}

[[nodiscard]]
static inline void *tda_realloc_n_(tda_Al *al, void *ptr, size_t old_count, size_t new_count, size_t size) {
    size_t new_bytes;
    if (ckd_mul(&new_bytes, new_count, size)) {
        return nullptr;
    }

    // 'ptr' was allocated as old_count * size, so that product is known to fit
    return tda_realloc(al, ptr, old_count * size, new_bytes);
}
/// @endcond

/// tda_alloc for 'count' elems of T, with the multiplication checked
/// @param T the elem type
/// @param al the allocator
/// @param count how many elems; an overflowing count gives null
#define TDA_ALLOC(T, al, count) \
    ((T *) tda_alloc_n_((al), (count), sizeof(T)))

/// tda_calloc for 'count' elems of T; tda_calloc checks the overflow
/// @param T the elem type
/// @param al the allocator
/// @param count how many elems
#define TDA_CALLOC(T, al, count) \
    ((T*) tda_calloc((al), (count), sizeof(T)))

/// tda_realloc between two counts of T, with the multiplication checked
/// @param T the elem type
/// @param al the allocator
/// @param ptr the block to resize
/// @param old_count what it holds now
/// @param new_count what it should hold; one that would overflow gives null
#define TDA_REALLOC(T, al, ptr, old_count, new_count) \
    ((T *) tda_realloc_n_((al), (ptr), (old_count), (new_count), sizeof(T)))

/// tda_dealloc for 'count' elems of T
/// @param T the elem type
/// @param al the allocator
/// @param ptr the block
/// @param count what it holds
#define TDA_DEALLOC(T, al, ptr, count) \
    tda_dealloc((al), (ptr), (count) * sizeof(T))

/// @}

/// @}
