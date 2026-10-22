#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"

#include <stddef.h>

/// @file

/// @defgroup alloc_aligned alloc/aligned
/// @ingroup alloc
/// @brief an allocator whose every block is over-aligned
///
/// tda_Al ops take no alignment, so alignment is a property of the allocator. A type that
/// needs more than max_align_t gets it by handing this allocator to its container, whose
/// code stays the same. That holds for the containers that keep their elems in one
/// buffer: arr, vec, deque, stack, queue and pqueue. list, hmap and hset place an elem at
/// a fixed offset inside a node, so it is aligned to max_align_t at most, whatever the
/// node is aligned to.
///
/// Each request goes to the parent 'alignment' - 1 + sizeof(void *) bytes larger. The
/// block is advanced to the next aligned address, and the parent's address is stored in
/// the word before it.
///
/// It has no calloc or realloc of its own: the wrappers build both from this alloc,
/// which keeps the result aligned (the parent's realloc would not). The parent is
/// borrowed and must outlive it.
///
/// @par Example
/// @snippet alloc/example_aligned.c build
/// @{

/// @name lifetime
/// @{

/// an allocator handing out blocks aligned to 'alignment', taken from 'parent'
/// @param parent where the memory comes from; borrowed
/// @param alignment a power of two, at least alignof(max_align_t); below that the parent
///                  already aligns enough
/// @return the allocator, or null if 'parent' could not give its two blocks
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_al_aligned_new(tda_Al *parent, size_t alignment);

/// gives its own two blocks back to the parent
/// @param self the allocator; null is a no-op
/// @warning frees nothing it handed out; give every block back through tda_dealloc
///          first, as with tda_al_default
/// @bigo{1}
TDA_API
void tda_al_aligned_drop(tda_Al *self);

/// @}

/// @}
