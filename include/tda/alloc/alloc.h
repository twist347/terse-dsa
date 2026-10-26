#pragma once

#include "tda/core/export.h"
#include "tda/core/layout.h"

#include <assert.h>
#include <stdckdint.h>
#include <stddef.h>

/// @file

/// @defgroup alloc_alloc alloc/alloc
/// @ingroup alloc
/// @brief tda_Al — the allocator interface every owning type is built on
///
/// An interface to dispatch through, so the ops are named for the verb and take the
/// allocator first, as fprintf takes its FILE: tda_alloc(al, layout). Only ops on the
/// allocator itself carry a slug, as tda_al_default and tda_al_eq do.
///
/// The hooks are Zig's: alloc, resize in place, remap that may move, dealloc. To them
/// Rust's alloc_zeroed adds a way to skip the memset where the memory comes zeroed
/// anyway, as calloc's large blocks do. alloc and dealloc are required, the rest
/// optional: a null hook means "never", and the wrappers fall back to the required two.
///
/// Every call carries the layout the block was made with, size and alignment both. So
/// the allocator keeps neither next to the block, a free knows how an over-aligned block
/// has to go back, and a checking allocator can compare what returns with what went out.
///
/// Null from alloc means the request cannot be met: out of memory, or a size past a
/// fixed-size allocator's block. An alignment the allocator was not built to give, as
/// one past a pool's, is a mistake in how it was set up, and asserts. Null from remap
/// means only that the allocator has no cheaper way to move the block than alloc, copy
/// and dealloc.
///
/// The wrappers return the pointer and signal failure with null. This is the one place a
/// fallible op returns no tda_Status; [[nodiscard]] on the pointer catches the same
/// dropped check. A request for 0 bytes gives null and is not an error.
///
/// An allocator is two things: its state, a value the caller holds, and a view of it to
/// hand around, the tda_Al. The view points at the state and at the hooks, one static
/// table of them for each kind of allocator, and each kind gives its view through
/// tda_<slug>_to_al. So the state can be copied and moved as any value can until a view
/// is out, and must stay where it is while one is: the view points at it, and so does
/// every container built on it. Nothing checks this, as nothing checks a span.
///
/// An allocator over another borrows it: the parent outlives the child, and they are
/// released in reverse order of building.
///
/// `= {}` is no allocator. An op that takes one optionally says so; every other asserts.
///
/// @par Example
/// @snippet alloc/example_alloc.c use
/// @snippet alloc/example_alloc.c custom
/// @snippet alloc/example_alloc.c wrap
/// @{

/// The hooks of one kind of allocator: one static table per kind, never one per
/// allocator. A table may grow in a minor, by optional hooks at the end only, so build
/// one with designated initializers.
typedef struct {
    /// 'layout.size' bytes, uninitialized, aligned to 'layout.align', or null when the
    /// request cannot be met. The size is never 0. Required.
    void *(*alloc)(void *ctx, tda_Layout layout);

    /// as alloc, zeroed. Optional: null means alloc and memset.
    void *(*alloc_zeroed)(void *ctx, tda_Layout layout);

    /// whether the block at 'ptr' now holds 'new_size' bytes without having moved; on
    /// false it is untouched. Neither size is 0. Optional: null means never.
    bool (*resize)(void *ctx, void *ptr, tda_Layout old, size_t new_size);

    /// the block at 'ptr' resized to 'new_size' bytes and maybe moved, still aligned to
    /// 'old.align', its contents kept up to the smaller size, or null with 'ptr' untouched
    /// when the allocator has no way cheaper than alloc, copy and dealloc, out of memory
    /// included. Neither size is 0. Optional: null means never.
    void *(*remap)(void *ctx, void *ptr, tda_Layout old, size_t new_size);

    /// gives back the block at 'ptr', made with 'layout'. Never null. Required.
    void (*dealloc)(void *ctx, void *ptr, tda_Layout layout);
} tda_AlVtable;

/// A view of an allocator: its state and its hooks. Small, and passed by value; valid
/// while the state stays where it is. `= {}` is no allocator.
typedef struct {
    void *ctx;              ///< the allocator's state, passed to every hook as is
    const tda_AlVtable *vt; ///< its hooks; null only in `= {}`
} tda_Al;

/// @name wrappers
/// @{

/// a block of 'layout', uninitialized
/// @param al the allocator
/// @param layout its size, of which 0 gives null and is not an error, and its alignment
/// @return the block, or null; for a non-zero size, null means the request cannot be met
/// @bigo{1} on top of the alloc hook
[[nodiscard]] TDA_API
void *tda_alloc(tda_Al al, tda_Layout layout);

/// a block of 'layout', zeroed
/// @param al the allocator
/// @param layout as for tda_alloc
/// @return the block, or null as for tda_alloc
/// @bigo{n} without an alloc_zeroed hook, for zeroing the block; otherwise that of
///       the alloc_zeroed hook
[[nodiscard]] TDA_API
void *tda_alloc_zeroed(tda_Al al, tda_Layout layout);

/// whether the block at 'ptr' now holds 'new_size' bytes, in place
/// @param al the allocator
/// @param ptr the block, not null
/// @param old what 'ptr' was made with; its size not 0
/// @param new_size what it should hold, not 0
/// @return true when the block was resized; on false it is untouched
/// @bigo{1} on top of the resize hook
[[nodiscard]] TDA_API
bool tda_resize(tda_Al al, void *ptr, tda_Layout old, size_t new_size);

/// the block at 'ptr' resized by the allocator's own move, or null when it has none
/// @param al the allocator
/// @param ptr the block, not null
/// @param old what 'ptr' was made with; its size not 0
/// @param new_size what it should hold, not 0
/// @return the block, which may have moved, or null with 'ptr' untouched
/// @note what an allocator over another calls to pass a remap on; a caller that just
///       wants the block resized calls tda_realloc
/// @bigo{1} on top of the remap hook
[[nodiscard]] TDA_API
void *tda_remap(tda_Al al, void *ptr, tda_Layout old, size_t new_size);

/// the block at 'ptr', resized to 'new_size' bytes: in place, else by the allocator's
/// remap, else by alloc, copy and dealloc
/// @param al the allocator
/// @param ptr the block to resize; null exactly when 'old.size' is 0
/// @param old what 'ptr' was made with; a moved block keeps its alignment
/// @param new_size what it should hold; 0 frees 'ptr' and returns null, not a failure
/// @return the block, which may have moved, or null on failure with 'ptr' untouched
/// @warning assign to a temporary, not over 'ptr': on failure the old block is still
///          yours, and overwriting its only pointer leaks it
/// @bigo{n} when the block moves by a copy; otherwise that of the resize or the remap
///       hook
[[nodiscard]] TDA_API
void *tda_realloc(tda_Al al, void *ptr, tda_Layout old, size_t new_size);

/// gives the block back
/// @param al the allocator; must be the one 'ptr' came from
/// @param ptr the block; null is a no-op
/// @param layout what 'ptr' was made with, size and alignment; must match
/// @bigo{1} on top of the dealloc hook
TDA_API
void tda_dealloc(tda_Al al, void *ptr, tda_Layout layout);

/// @}

/// @name compare
/// @{

/// whether two views are of one allocator, so a block from either goes back through the
/// other: the same state and the same hooks. An allocator over another is not that
/// other, even when it passes every request on
/// @param a one view
/// @param b the other
/// @return whether both the state and the hooks match
/// @bigo{1}
[[nodiscard]] TDA_API
bool tda_al_eq(tda_Al a, tda_Al b);

/// @}

/// @name macros
/// @{

/// @cond
// what the macros expand to; a function evaluates each argument once, while a macro
// checking the product would evaluate the count twice

[[nodiscard]]
static inline void *tda_alloc_n_(tda_Al al, tda_Layout elem, size_t count, bool zeroed) {
    tda_Layout layout;
    if (tda_layout_ckd_array(&layout, elem, count)) {
        return nullptr;
    }

    return zeroed ? tda_alloc_zeroed(al, layout) : tda_alloc(al, layout);
}

[[nodiscard]]
static inline void *tda_realloc_n_(tda_Al al, void *ptr, tda_Layout elem, size_t old_count, size_t new_count) {
    tda_Layout new_layout;
    if (tda_layout_ckd_array(&new_layout, elem, new_count)) {
        return nullptr;
    }

    // 'ptr' was made as old_count elems, so that product is known to fit
    const tda_Layout old = {.size = old_count * elem.size, .align = elem.align};

    return tda_realloc(al, ptr, old, new_layout.size);
}

static inline void tda_dealloc_n_(tda_Al al, void *ptr, tda_Layout elem, size_t count) {
    // 'ptr' was made as count elems, so the product fits; the call checks the elem layout
    tda_Layout layout;
    [[maybe_unused]] const bool overflow = tda_layout_ckd_array(&layout, elem, count);
    assert(!overflow);

    tda_dealloc(al, ptr, layout);
}
/// @endcond

/// tda_alloc for 'count' elems of T, with the multiplication checked
/// @param T the elem type, which gives the size and the alignment
/// @param al the allocator
/// @param count how many elems; an overflowing count gives null
#define TDA_ALLOC(T, al, count) \
    ((T *) tda_alloc_n_((al), TDA_LAYOUT_OF(T), (count), false))

/// tda_alloc_zeroed for 'count' elems of T, with the multiplication checked
/// @param T the elem type
/// @param al the allocator
/// @param count how many elems; an overflowing count gives null
#define TDA_ALLOC_ZEROED(T, al, count) \
    ((T *) tda_alloc_n_((al), TDA_LAYOUT_OF(T), (count), true))

/// tda_realloc between two counts of T, with the multiplication checked
/// @param T the elem type
/// @param al the allocator
/// @param ptr the block to resize
/// @param old_count what it holds now
/// @param new_count what it should hold; one that would overflow gives null
#define TDA_REALLOC(T, al, ptr, old_count, new_count) \
    ((T *) tda_realloc_n_((al), (ptr), TDA_LAYOUT_OF(T), (old_count), (new_count)))

/// tda_dealloc for 'count' elems of T
/// @param T the elem type
/// @param al the allocator
/// @param ptr the block
/// @param count what it holds
#define TDA_DEALLOC(T, al, ptr, count) \
    tda_dealloc_n_((al), (ptr), TDA_LAYOUT_OF(T), (count))

/// @}

/// @}
