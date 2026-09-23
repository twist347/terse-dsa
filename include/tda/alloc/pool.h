#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"

#include <stddef.h>

/// @file

/// @defgroup alloc_pool alloc/pool
/// @ingroup alloc
/// @brief an allocator that hands out blocks of one fixed size
///
/// One block, cut into equal pieces on a free list, so alloc and dealloc are both a
/// pointer move and nothing fragments. The price is that every request must fit one
/// piece.
///
/// tda_realloc keeps a block where it is while the new size fits it, and fails once it
/// does not. The parent is borrowed and has to outlive it; tda_al_pool_from_buf needs
/// none, and runs on memory the caller already has.
///
/// @par Example
/// @snippet alloc/example_pool.c build
/// @snippet alloc/example_pool.c limits
/// @{

/// @name lifetime
/// @{

/// a pool of 'block_count' blocks of 'block_size' bytes, taken from 'parent'
/// @param parent where the block comes from, borrowed and not owned
/// @param block_size a floor, greater than 0: it is rounded up to the alignment and to
///                   what the free list needs, and tda_al_pool_stats reports the result
/// @param block_count how many blocks, greater than 0
/// @return the allocator, or null if the product overflowed or 'parent' had no block
/// @bigo{n} — the free list is threaded through every block
[[nodiscard]] TDA_API
tda_Al *tda_al_pool_new(tda_Al *parent, size_t block_size, size_t block_count);

/// a pool inside 'buf', with no parent: its own header takes the front, aligned, and as
/// many blocks as fit the rest
/// @param buf the memory, borrowed and not owned — an array, static or on the stack, or a
///            block from anywhere; any alignment
/// @param size its bytes, greater than 0
/// @param block_size a floor, greater than 0, rounded up as in tda_al_pool_new;
///                   tda_al_pool_stats reports it and the block count
/// @return the allocator, which points into 'buf', or null if 'buf' cannot hold the
///         header and one block
/// @warning 'buf' has to outlive the pool and everything built on it
/// @bigo{n} — the free list is threaded through every block
[[nodiscard]] TDA_API
tda_Al *tda_al_pool_from_buf(void *buf, size_t size, size_t block_size);

/// gives the block back to the parent; from tda_al_pool_from_buf there is nothing to give
/// @param self the pool; null is a no-op
/// @bigo{1}
TDA_API
void tda_al_pool_drop(tda_Al *self);

/// @}

/// @name mods
/// @{

/// takes every block back at once, leaving the pool as new
/// @param self the pool
/// @warning every pointer it ever handed out dies here
/// @bigo{n} — the free list is threaded again
TDA_API
void tda_al_pool_reset(tda_Al *self);

/// @}

/// @name stats
/// @{

/// What a pool is holding.
typedef struct {
    size_t block_size;  ///< the real size of one block, after the rounding up
    size_t block_count; ///< how many there are in all
    size_t used;        ///< how many are handed out
    size_t free;        ///< how many are left
} tda_AlPoolStats;

/// what the pool is holding
/// @param self the pool
/// @return the four numbers
/// @bigo{1}
[[nodiscard]] TDA_API
tda_AlPoolStats tda_al_pool_stats(const tda_Al *self);

/// @}

/// @}
