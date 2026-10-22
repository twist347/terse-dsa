#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"

#include <stddef.h>

/// @file

/// @defgroup alloc_slab alloc/slab
/// @ingroup alloc
/// @brief an allocator that keeps small blocks in pools by size class
///
/// A request of up to 512 bytes is rounded up to one of a few size classes, from 16 to
/// 512, and served from that class's pool; anything larger goes to the parent as it is.
/// Each pool takes 4 KiB chunks from the parent as it needs them and threads freed
/// blocks onto a list of its own, so alloc and dealloc of a small block are a pointer
/// move.
///
/// This is the allocator to put under a container. The struct of a container, the
/// nodes of a list or an hmap and the small arrays all land in pools, while the buffer
/// of a grown vec goes to the parent, so every container runs on it with no tuning.
/// alloc/pool, by contrast, holds one size only.
///
/// The price of a class is the rounding: a 20-byte list node takes a 32-byte block. A
/// freed block goes back to its pool, not to the parent, so the memory a peak took
/// stays held until tda_al_slab_drop.
///
/// No block carries a header: the class is found again from the size handed to
/// tda_dealloc, which is why that size must match.
///
/// @par Example
/// @snippet alloc/example_slab.c build
/// @{

/// @name lifetime
/// @{

/// a slab over 'parent'
/// @param parent where the chunks and the large blocks come from; borrowed
/// @return the allocator, or null if 'parent' could not give its two blocks
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_al_slab_new(tda_Al *parent);

/// gives every chunk back to the parent, and with them every small block
/// @param self the slab; null is a no-op
/// @warning a large block belongs to the parent; give each back through tda_dealloc
///          first, as with tda_al_default
/// @bigo{n} in the chunks held
TDA_API
void tda_al_slab_drop(tda_Al *self);

/// @}

/// @name stats
/// @{

/// What a slab is holding.
typedef struct {
    size_t chunks;       ///< chunks taken from the parent
    size_t chunk_bytes;  ///< their bytes in all
    size_t small_blocks; ///< small blocks handed out and not given back
    size_t large_blocks; ///< large blocks, passed to the parent, not given back
} tda_AlSlabStats;

/// what the slab is holding
/// @param self the slab
/// @return the four numbers
/// @bigo{1}
[[nodiscard]] TDA_API
tda_AlSlabStats tda_al_slab_stats(const tda_Al *self);

/// @}

/// @}
