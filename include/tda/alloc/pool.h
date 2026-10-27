#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"
#include "tda/core/status.h"

#include <stddef.h>

/// @file

/// @defgroup alloc_pool alloc/pool
/// @ingroup alloc
/// @brief an allocator that hands out blocks of one fixed layout
///
/// One block, cut into equal pieces on a free list, so alloc and dealloc are both a
/// pointer move and nothing fragments. The price is that every request must fit one
/// piece: a larger size gets null, as when the pieces run out, and a larger alignment
/// asserts, since that is how the pool was built and not how full it is.
///
/// A block resizes in place while the new size fits it, and not past that. The parent
/// is borrowed and has to outlive it; tda_pool_init_buf needs none, and runs on
/// memory the caller already has.
///
/// So a pool suits objects of one type, as TDA_LAYOUT_OF gives the block, not most
/// containers. A list asks for nodes alone and works on blocks of tda_list_node_layout.
/// vec, deque, the adapters and hmap grow a buffer (hmap its bucket array) and get
/// TDA_STATUS_ERR_NO_MEM once it outgrows a block. Under a container, alloc/slab is the
/// pool to use: one per size class.
///
/// A block freed twice goes onto the free list twice and is then handed out to two
/// owners. The check for it is partial: it catches the second free when nothing was freed
/// in between, or when no other block is taken; free x, free y, free x passes, since
/// catching that means walking the list.
///
/// @par Example
/// @snippet alloc/example_pool.c build
/// @snippet alloc/example_pool.c limits
/// @{

/// A pool. A value the caller holds, on the stack or inside a struct: tda_pool_init_cap
/// or tda_pool_init_buf fills it in, tda_pool_to_al hands out views of it, and
/// tda_pool_deinit gives the blocks back. `= {}` is the inert state.
///
/// The fields belong to the pool: read and change them only through the functions here.
///
/// @warning once a view from tda_pool_to_al is out, the pool must not move nor be copied:
///          the view points at it, and so does every container built on it. While no view
///          is out, a copy is a second owner of the blocks, as a copy of a container is.
typedef struct tda_Pool {
    tda_Al parent_;                   ///< where the blocks came from; `= {}` for a buffer
    unsigned char *data_;             ///< the first block; null only in the inert state
    struct tda_PoolFree_ *free_head_; ///< the first free block
    size_t block_size_;               ///< the size of one block, rounded up
    size_t block_align_;              ///< its alignment
    size_t block_count_;              ///< how many there are
    size_t used_;                     ///< how many are handed out
} tda_Pool;

/// @name lifetime
/// @{

/// a pool of 'block_count' blocks of 'block', taken from 'parent'
/// @param block_count how many blocks, greater than 0
/// @param block a floor, its size greater than 0: raised to what the free list needs,
///              its size then rounded up to its alignment; tda_pool_stats reports
///              the result
/// @param parent where the blocks come from; borrowed, and must outlive the pool. A reset
///               or rewind of it takes the pool's block back while the pool still
///               hands it out
/// @param[out] out the pool, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the product overflows or 'parent' cannot give
///         the blocks
/// @bigo{n} — the free list is threaded through every block
[[nodiscard]] TDA_API
tda_Status tda_pool_init_cap(size_t block_count, tda_Layout block, tda_Al parent, tda_Pool *out);

/// a pool over 'buf', with no parent: as many blocks as fit once the start is aligned
/// @param buf the memory, borrowed: a static or stack array, or a block from anywhere;
///            any alignment
/// @param cap its bytes, greater than 0
/// @param block a floor, raised as in tda_pool_init_cap; tda_pool_stats reports it and
///              the block count
/// @param[out] out the pool, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when 'buf' cannot hold one aligned block
/// @warning 'buf' has to outlive the pool and everything built on it
/// @bigo{n} — the free list is threaded through every block
[[nodiscard]] TDA_API
tda_Status tda_pool_init_buf(void *buf, size_t cap, tda_Layout block, tda_Pool *out);

/// gives the blocks back to the parent, a no-op for tda_pool_init_buf, and leaves the
/// pool inert
/// @param self a pool, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_pool_deinit(tda_Pool *self);

/// @}

/// @name view
/// @{

/// a view of the pool, to hand to a container or a wrapper
/// @param self the pool; from now on it must stay where it is
/// @return the view, valid while the pool stays where it is and until its deinit
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al tda_pool_to_al(tda_Pool *self);

/// @}

/// @name mods
/// @{

/// takes every block back at once, leaving the pool as new
/// @param self the pool
/// @warning every pointer it ever handed out dies here
/// @bigo{n} — the free list is threaded again
TDA_API
void tda_pool_reset(tda_Pool *self);

/// @}

/// @name stats
/// @{

/// What a pool is holding.
typedef struct {
    size_t block_size;  ///< the real size of one block, after the rounding up
    size_t block_align; ///< its real alignment, the most a request may ask for
    size_t blocks;      ///< how many there are in all
    size_t used;        ///< how many are handed out
    size_t free;        ///< how many are left
} tda_PoolStats;

/// what the pool is holding
/// @param self the pool
/// @return what it holds
/// @bigo{1}
[[nodiscard]] TDA_API
tda_PoolStats tda_pool_stats(const tda_Pool *self);

/// @}

/// @}
