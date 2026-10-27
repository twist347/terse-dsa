#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"
#include "tda/core/status.h"

#include <stddef.h>

/// @file

/// @defgroup alloc_arena alloc/arena
/// @ingroup alloc
/// @brief an allocator that only ever bumps a pointer forward
///
/// One block, taken from the parent at construction and handed out in pieces. dealloc is
/// a no-op; everything is freed at once by tda_arena_reset in O(1).
///
/// Each block starts on the alignment it asks for and takes its size and no more, so the
/// only waste is the padding that brings the cursor up to a block's alignment: none
/// between blocks of one alignment whose sizes keep it, up to align - 1 bytes before one
/// that asks for more than the block before it left. The last block resizes in
/// place, since nothing lies past it; any other block shrinks in place and moves to grow,
/// its old slot staying used. A vec growing alone costs the arena its final
/// capacity; one growing beside others costs every capacity it passed through. A deque
/// and an hmap always grow into a new buffer, so they cost every capacity even alone.
/// The parent is borrowed and must outlive the arena; tda_arena_init_buf needs no
/// parent and uses memory the caller already has. The arena itself is a value the caller
/// holds, so making one allocates nothing beyond its block.
///
/// tda_arena_mark and tda_arena_rewind give scratch memory inside a long-lived
/// arena: a per-frame arena can lend room to a pathfind and take it back without a reset.
/// Marks nest and are rewound in reverse order. A mark also sets a floor: a block from
/// below it moves instead of growing in place past it, so a rewind never cuts off the
/// tail of a block that outlives the mark.
///
/// @warning what outlives a mark must not allocate before the rewind. A container made
///          before the mark that grows after it moves into memory above the mark, and
///          the rewind hands that memory out again under it.
///
/// @par Example
/// @snippet alloc/example_arena.c build
/// @snippet alloc/example_arena.c reset
/// @snippet alloc/example_arena.c mark
/// @snippet alloc/example_arena.c buf
/// @{

/// An arena. A value the caller holds, on the stack or inside a struct:
/// tda_arena_init_cap or tda_arena_init_buf makes it, tda_arena_to_al hands out views of
/// it, and tda_arena_deinit gives the block back. `= {}` is the inert state.
///
/// The fields belong to the arena: read and change them only through the functions here.
///
/// @warning once a view from tda_arena_to_al is out, the arena must not move nor be
///          copied: the view points at it, and so does every container built on it. While
///          no view is out, a copy is a second owner of the block, as a copy of a
///          container is.
typedef struct tda_Arena {
    tda_Al parent_;       ///< where the block came from; `= {}` for a caller's buffer
    unsigned char *data_; ///< the block; null only in the inert state
    size_t cap_;          ///< its bytes
    size_t offset_;       ///< how far the pointer is bumped
    /// the offset of the latest live mark. A last block starting below it moves to grow,
    /// so a rewind to the mark never cuts it short
    size_t floor_;
} tda_Arena;

/// @name lifetime
/// @{

/// an arena over 'cap' bytes taken from 'parent'
/// @param cap the most bytes it will hand out, padding included, greater than 0
/// @param parent where the block comes from; borrowed, and must outlive the arena
/// @param[out] out the arena, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when 'parent' cannot give the block
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_arena_init_cap(size_t cap, tda_Al parent, tda_Arena *out);

/// an arena over 'buf', with no parent: all of it is the block
/// @param buf the memory, borrowed: a static or stack array, or a block from anywhere;
///            any alignment
/// @param cap its bytes, greater than 0
/// @return the arena
/// @warning 'buf' has to outlive the arena and everything built on it
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Arena tda_arena_init_buf(void *buf, size_t cap);

/// gives the block back to the parent, a no-op for tda_arena_init_buf, and leaves the
/// arena inert
/// @param self an arena, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_arena_deinit(tda_Arena *self);

/// @}

/// @name view
/// @{

/// a view of the arena, to hand to a container or a wrapper
/// @param self the arena; from now on it must stay where it is
/// @return the view, valid while the arena stays where it is and until its deinit
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al tda_arena_to_al(tda_Arena *self);

/// @}

/// @name mods
/// @{

/// takes everything back at once, leaving the arena as new
/// @param self the arena
/// @warning invalidates every pointer it handed out and every mark taken on it
/// @bigo{1}
TDA_API
void tda_arena_reset(tda_Arena *self);

/// A saved arena position, to rewind to.
/// Pass it back to tda_arena_rewind; the fields are internal
typedef struct {
    size_t offset_; ///< how far the pointer was bumped
    size_t floor_;  ///< the floor before this mark, which the rewind puts back
} tda_ArenaMark;

/// saves the arena position and raises the floor to it: a block handed out before the
/// mark now moves to grow instead of growing in place past it
/// @param self the arena
/// @return the mark, for tda_arena_rewind; an unused mark costs only the in-place
///         growth of the blocks below it
/// @bigo{1}
[[nodiscard]] TDA_API
tda_ArenaMark tda_arena_mark(tda_Arena *self);

/// takes back everything handed out since 'mark' and restores the floor
/// @param self the arena
/// @param mark taken on this arena and not since passed by a rewind to an earlier mark
///             or a reset. A mark already rewound past is caught; one from another
///             arena, or one the arena has since been refilled past, cannot be told
///             from a live one
/// @warning invalidates every pointer handed out since the mark and every later mark
/// @bigo{1}
TDA_API
void tda_arena_rewind(tda_Arena *self, tda_ArenaMark mark);

/// @}

/// @name stats
/// @{

/// What an arena is holding.
typedef struct {
    size_t cap;  ///< the block it was built with, taken as given
    size_t used; ///< how far the pointer is bumped, padding included
    size_t free; ///< cap - used, an upper bound on the next request
} tda_ArenaStats;

/// what the arena is holding
/// @param self the arena
/// @return what it holds
/// @bigo{1}
[[nodiscard]] TDA_API
tda_ArenaStats tda_arena_stats(const tda_Arena *self);

/// @}

/// @}
