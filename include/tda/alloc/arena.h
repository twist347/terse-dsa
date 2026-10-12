#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"

#include <stddef.h>

/// @file

/// @defgroup alloc_arena alloc/arena
/// @ingroup alloc
/// @brief an allocator that only ever bumps a pointer forward
///
/// One block, taken from the parent at construction and handed out in pieces. dealloc is
/// a no-op; everything is freed at once by tda_al_arena_reset in O(1).
///
/// tda_realloc resizes the last block in place, since nothing lies past it; any other
/// block moves and its old slot stays used. A vec growing alone costs the arena its final
/// capacity; one growing beside others costs every capacity it passed through. The parent
/// is borrowed and must outlive the arena; tda_al_arena_from_buf needs no parent and uses
/// memory the caller already has.
///
/// tda_al_arena_mark and tda_al_arena_rewind give scratch memory inside a long-lived
/// arena: a per-frame arena can lend room to a pathfind and take it back without a reset.
/// Marks nest and are rewound in reverse order. A mark also sets a floor: a block from
/// below it moves instead of growing in place past it, so a rewind never cuts off the
/// tail of a block that outlives the mark.
///
/// @par Example
/// @snippet alloc/example_arena.c build
/// @snippet alloc/example_arena.c reset
/// @snippet alloc/example_arena.c mark
/// @snippet alloc/example_arena.c buf
/// @{

/// @name lifetime
/// @{

/// an arena over 'cap' bytes taken from 'parent'
/// @param parent where the block comes from; borrowed
/// @param cap the most bytes it will hand out, greater than 0; rounded up to the
///            alignment, as every request is
/// @return the allocator, or null if 'parent' could not give the block
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_al_arena_new(tda_Al *parent, size_t cap);

/// an arena inside 'buf', with no parent: its header takes the aligned front, and the
/// rest, cut down to a multiple of the alignment, is the block
/// @param buf the memory, borrowed: a static or stack array, or a block from anywhere;
///            any alignment
/// @param size its bytes, greater than 0
/// @return the allocator, which points into 'buf', or null if 'buf' is too small to hold
///         the header and one aligned slot past it
/// @warning 'buf' has to outlive the arena and everything built on it
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_al_arena_from_buf(void *buf, size_t size);

/// gives the block back to the parent; a no-op for tda_al_arena_from_buf
/// @param self the arena; null is a no-op
/// @bigo{1}
TDA_API
void tda_al_arena_drop(tda_Al *self);

/// @}

/// @name mods
/// @{

/// takes everything back at once, leaving the arena as new
/// @param self the arena
/// @warning invalidates every pointer it handed out and every mark taken on it
/// @bigo{1}
TDA_API
void tda_al_arena_reset(tda_Al *self);

/// A saved arena position, to rewind to.
/// Pass it back to tda_al_arena_rewind; the fields are internal
typedef struct {
    const void *arena; ///< the arena it was taken on
    size_t offset;     ///< how far the pointer was bumped
    size_t floor;      ///< the floor before this mark, which the rewind puts back
} tda_AlArenaMark;

/// saves the arena position and raises the floor to it: a block handed out before the
/// mark now moves to grow instead of growing in place past it
/// @param self the arena
/// @return the mark, for tda_al_arena_rewind; an unused mark costs only the in-place
///         growth of the blocks below it
/// @bigo{1}
[[nodiscard]] TDA_API
tda_AlArenaMark tda_al_arena_mark(tda_Al *self);

/// takes back everything handed out since 'mark' and restores the floor
/// @param self the arena
/// @param mark taken on this arena and not since passed by a rewind to an earlier mark
///             or a reset; asserted where possible, but a mark the arena has since been
///             refilled past cannot be told from a live one
/// @warning invalidates every pointer handed out since the mark and every later mark
/// @bigo{1}
TDA_API
void tda_al_arena_rewind(tda_Al *self, tda_AlArenaMark mark);

/// @}

/// @name stats
/// @{

/// What an arena is holding.
typedef struct {
    size_t cap;       ///< the block it was built with, a multiple of the alignment
    size_t used;      ///< how far the pointer is bumped, padding included
    size_t available; ///< cap - used, an upper bound on the next request
} tda_AlArenaStats;

/// what the arena is holding
/// @param self the arena
/// @return the three numbers
/// @bigo{1}
[[nodiscard]] TDA_API
tda_AlArenaStats tda_al_arena_stats(const tda_Al *self);

/// @}

/// @}
