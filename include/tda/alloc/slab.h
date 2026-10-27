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
/// A class gives the alignments that divide it, 16 at least, so a request at 64 lands in
/// the 64-byte class or above; one aligned past 512 goes to the parent too.
/// Each pool takes 4 KiB chunks from the parent as it needs them and threads freed
/// blocks onto a list of its own, so alloc and dealloc of a small block are a pointer
/// move.
///
/// This is the allocator to put under a container. The nodes of a list or an hmap and
/// the small arrays all land in pools, while the buffer of a grown vec goes to the
/// parent, so every container runs on it with no tuning.
/// alloc/pool, by contrast, holds one size only.
///
/// The price of a class is the rounding: the 24-byte node of a list of int32_t takes a 32-byte block. A
/// freed block goes back to its pool, not to the parent, so the memory a peak took
/// stays held until tda_slab_deinit.
///
/// No block carries a header: the class is found again from the layout handed to
/// tda_dealloc, which is why that layout must match.
///
/// @par Example
/// @snippet alloc/example_slab.c build
/// @{

/// @cond
// one size class, carved from the slab's chunks; the slab's own, here only so the struct
// below has a size
typedef struct {
    struct tda_SlabFree_ *free_head; // freed blocks, taken first
    unsigned char *carve;            // the uncarved rest of the newest chunk
    unsigned char *carve_end;
} tda_SlabClass_;

// how many classes there are; the source checks it against its table
enum { TDA_SLAB_CLASSES_ = 10 };
/// @endcond

/// A slab. A value the caller holds, on the stack or inside a struct: tda_slab_init
/// makes it, tda_slab_to_al hands out views of it, and tda_slab_deinit gives its chunks
/// back. `= {}` is the inert state.
///
/// The fields belong to the slab: read and change them only through the functions here.
///
/// @warning once a view from tda_slab_to_al is out, the slab must not move nor be copied:
///          the view points at it, and so does every container built on it. While no view
///          is out, a copy is a second owner of the chunks, as a copy of a container is.
typedef struct tda_Slab {
    tda_Al parent_;                             ///< where chunks and large blocks come from
    struct tda_SlabChunk_ *chunks_;             ///< every chunk taken, newest first
    size_t chunk_count_;                        ///< how many
    size_t small_blocks_;                       ///< small blocks out
    size_t large_blocks_;                       ///< large blocks out, the parent's
    tda_SlabClass_ classes_[TDA_SLAB_CLASSES_]; ///< one pool per size
} tda_Slab;

/// @name lifetime
/// @{

/// a slab over 'parent'; takes nothing until the first request, so it cannot fail
/// @param parent where the chunks and the large blocks come from; borrowed, and must
///               outlive the slab. A reset or rewind of it takes the chunks back while
///               the slab still hands out their blocks
/// @return the slab
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Slab tda_slab_init(tda_Al parent);

/// gives every chunk back to the parent, and with them every small block, and leaves
/// the slab inert
/// @param self a slab, or an inert one, for which this is a no-op
/// @warning a large block belongs to the parent; give each back through tda_dealloc
///          first, as with tda_al_default
/// @bigo{n} in the chunks held
TDA_API
void tda_slab_deinit(tda_Slab *self);

/// @}

/// @name view
/// @{

/// a view of the slab, to hand to a container or a wrapper
/// @param self the slab; from now on it must stay where it is
/// @return the view, valid while the slab stays where it is and until its deinit
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al tda_slab_to_al(tda_Slab *self);

/// @}

/// @name stats
/// @{

/// What a slab is holding.
typedef struct {
    size_t chunks;       ///< chunks taken from the parent
    size_t chunk_bytes;  ///< their bytes in all
    size_t small_blocks; ///< small blocks handed out and not given back
    size_t large_blocks; ///< large blocks, passed to the parent, not given back
} tda_SlabStats;

/// what the slab is holding
/// @param self the slab
/// @return what it holds
/// @bigo{1}
[[nodiscard]] TDA_API
tda_SlabStats tda_slab_stats(const tda_Slab *self);

/// @}

/// @}
