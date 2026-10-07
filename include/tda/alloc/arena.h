#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"

#include <stddef.h>

/// @file

/// @defgroup alloc_arena alloc/arena
/// @ingroup alloc
/// @brief an allocator that only ever bumps a pointer forward
///
/// One block, taken from the parent at construction and handed out in pieces. There is no
/// per-block free: dealloc is a no-op, and everything returns at once through
/// tda_al_arena_reset, which drops a whole phase of work in O(1).
///
/// tda_realloc grows or shrinks the last block where it stands, since nothing lies past
/// it; any other block moves and leaves its old slot charged. A vec growing alone costs
/// the arena its capacity, one growing beside others every capacity it passed through.
/// The parent is borrowed and has to outlive it; tda_al_arena_from_buf needs none, and
/// runs on memory the caller already has.
///
/// Between the two ends sits tda_al_arena_mark / tda_al_arena_rewind: scratch memory
/// inside a long-lived arena, a frame's arena lending a pathfind or a string its room and
/// taking it back without dropping the frame. Marks nest, rewound in the reverse order
/// they were taken. A mark also sets a floor: a block from below it never grows in place
/// past it, but moves, so a rewind can never cut the tail off a block that outlives it.
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
/// @param parent where the block comes from, borrowed and not owned
/// @param cap how many bytes it will ever hand out, greater than 0; rounded up to
///            the alignment, since every request is
/// @return the allocator, or null if 'parent' could not give the block
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_al_arena_new(tda_Al *parent, size_t cap);

/// an arena inside 'buf', with no parent: its own header takes the front, aligned, and
/// the rest is the block, cut down to a multiple of the alignment
/// @param buf the memory, borrowed and not owned — an array, static or on the stack, or a
///            block from anywhere; any alignment
/// @param size its bytes, greater than 0
/// @return the allocator, which points into 'buf', or null if 'buf' is too small to hold
///         the header and one aligned slot past it
/// @warning 'buf' has to outlive the arena and everything built on it
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_al_arena_from_buf(void *buf, size_t size);

/// gives the block back to the parent; from tda_al_arena_from_buf there is nothing to give
/// @param self the arena; null is a no-op
/// @bigo{1}
TDA_API
void tda_al_arena_drop(tda_Al *self);

/// @}

/// @name mods
/// @{

/// takes everything back at once, leaving the arena as new
/// @param self the arena
/// @warning every pointer it ever handed out dies here, and every mark taken on it
/// @bigo{1}
TDA_API
void tda_al_arena_reset(tda_Al *self);

/// Where an arena stood, to rewind it to.
/// A value to hand back to tda_al_arena_rewind, not to read: the fields are the arena's
typedef struct {
    const void *arena; ///< the arena it was taken on
    size_t offset;     ///< how far the pointer was bumped
    size_t floor;      ///< the floor before this mark, which the rewind puts back
} tda_AlArenaMark;

/// marks where the arena stands, and raises its floor there: from now on a block handed
/// out before the mark that grows moves rather than growing in place past it
/// @param self the arena
/// @return the mark, for tda_al_arena_rewind; one never rewound costs nothing but the
///         in-place growth of the blocks below it
/// @bigo{1}
[[nodiscard]] TDA_API
tda_AlArenaMark tda_al_arena_mark(tda_Al *self);

/// takes back everything handed out since 'mark', and the floor with it
/// @param self the arena
/// @param mark taken on this arena and not rewound past since — by a rewind to an earlier
///             mark, or a reset — both asserted as far as they can be: a mark the arena has
///             since been refilled past cannot be told from a live one
/// @warning every pointer handed out since the mark dies here, and every mark taken
///          after it
/// @bigo{1}
TDA_API
void tda_al_arena_rewind(tda_Al *self, tda_AlArenaMark mark);

/// @}

/// @name stats
/// @{

/// What an arena is holding.
typedef struct {
    size_t cap;       ///< the block it was built with, a multiple of the alignment
    size_t used;      ///< how far the pointer is bumped, alignment padding and all
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
