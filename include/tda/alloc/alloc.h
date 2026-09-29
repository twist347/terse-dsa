#pragma once

#include "tda/core/export.h"

#include <stdckdint.h>
#include <stddef.h>

/// @file

/// @defgroup alloc_alloc alloc/alloc
/// @ingroup alloc
/// @brief tda_Al — the allocator everything that owns memory is built on
///
/// An interface you dispatch through, not an object you own, so the operations are named
/// for the verb and take the allocator first: tda_alloc(al, n), the way fprintf takes its
/// FILE. Only ops about the allocator itself carry a slug, as tda_al_default does.
///
/// The wrappers return the pointer and say failure with null — the one place a fallible
/// op returns no tda_Status, since [[nodiscard]] on the pointer flags the same dropped
/// check.
/// Null is not always failure: asking for nothing gives nothing, and each wrapper says
/// which of its nulls mean what.
///
/// A table must fill in alloc and dealloc and may leave calloc and realloc null — the
/// wrapper builds those out of the other two, which is how the aligned allocator lives,
/// and the arena and the pool for calloc.
/// An allocator over another borrows it: the parent outlives the child, and the two drop
/// in the reverse order of building.
///
/// @par Example
/// @snippet alloc/example_alloc.c use
/// @snippet alloc/example_alloc.c custom
/// @snippet alloc/example_alloc.c wrap
/// @{

/// Four operations and the state they run on, passed around by pointer.
typedef struct {
    void *ctx; ///< whatever the implementation keeps; handed back to every callback

    /// 'size' bytes, uninitialized, or null. Required.
    void *(*alloc)(void *ctx, size_t size);

    /// 'num' * 'size' zeroed bytes, or null. Optional: null here means alloc and memset.
    void *(*calloc)(void *ctx, size_t num, size_t size);

    /// 'ptr' moved from 'old_size' to 'new_size' bytes, or null. Optional: null here
    /// means alloc, copy, dealloc.
    void *(*realloc)(void *ctx, void *ptr, size_t old_size, size_t new_size);

    /// gives back 'size' bytes at 'ptr'. Required.
    void (*dealloc)(void *ctx, void *ptr, size_t size);
} tda_Al;

/// @name wrappers
/// @{

/// 'size' bytes, uninitialized
/// @param al the allocator
/// @param size how many bytes; 0 asks for nothing and gives null, which is defined
/// @return the block, or null — with 'size' over 0, null is out of memory and nothing else
[[nodiscard]] TDA_API
void *tda_alloc(tda_Al *al, size_t size);

/// 'num' * 'size' bytes, zeroed
/// @param al the allocator
/// @param num how many elems
/// @param size the size of one
/// @return the block, or null — either operand was 0, the product overflowed size_t, or
///         the allocation failed; on a non-zero, non-overflowing request only the last
[[nodiscard]] TDA_API
void *tda_calloc(tda_Al *al, size_t num, size_t size);

/// the block at 'ptr', resized to 'new_size' bytes
/// @param al the allocator
/// @param ptr the block to resize; may be null only when 'old_size' is 0
/// @param old_size what 'ptr' was allocated as
/// @param new_size what it should become; 0 gives 'ptr' back and returns null, defined
///                 and not a failure
/// @return the block, which may have moved, or null on failure with 'ptr' untouched
/// @warning assign to a temporary, never over 'ptr' — on failure the old block is still
///          yours, and overwriting its only pointer leaks it
[[nodiscard]] TDA_API
void *tda_realloc(tda_Al *al, void *ptr, size_t old_size, size_t new_size);

/// gives the block back
/// @param al the allocator; must be the one 'ptr' came from
/// @param ptr the block; null is a no-op
/// @param size what 'ptr' was allocated as; it must match, an allocator may need it
TDA_API
void tda_dealloc(tda_Al *al, void *ptr, size_t size);

/// @}

/// @name macros
/// @{

/// @cond
// what TDA_ALLOC and TDA_REALLOC expand to: a function reads each argument once, where a
// macro that checks the product before forming it has to read the count twice

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
/// @param count how many elems; one that would overflow gives null, not a wrapped request
#define TDA_ALLOC(T, al, count) \
    ((T *) tda_alloc_n_((al), (count), sizeof(T)))

/// tda_calloc for 'count' elems of T — the overflow check is tda_calloc's own
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
