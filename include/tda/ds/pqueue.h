#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/status.h"
#include "tda/ds/vec.h"

#include <stddef.h>

/// @file

/// @defgroup ds_pqueue ds/pqueue
/// @ingroup ds
/// @brief tda_PQueue — an owning queue that serves the greatest elem first
///
/// A growable buffer kept under the heap discipline of algo/heap, so the greatest elem by
/// 'cmp' is always the one at the front. A min-queue is this same type built with a
/// descending comparator (tda_cmp_desc_i32 and friends) — there is deliberately no second
/// type.
///
/// The comparator is fixed at construction and travels with the elems through copy and
/// swap, since a heap means nothing without the order it was built under.
///
/// There is deliberately no tda_pqueue_eq: equal contents do not make equal heaps, since
/// the same elems pushed in another order lie in another arrangement. An honest answer
/// would have to sort a copy. Two queues are compared by draining them.
///
/// Nothing here hands out a mutable elem — no top_mut, no get, no to_span_mut: a write
/// through one would break the heap invariant with no way for the queue to notice.
///
/// @par Example
/// @snippet ds/example_pqueue.c build
/// @snippet ds/example_pqueue.c serve
/// @snippet ds/example_pqueue.c order
/// @snippet ds/example_pqueue.c into
/// @{

/// Owning queue that serves the greatest elem first.
/// An opaque handle: it comes from a constructor and goes back to tda_pqueue_drop
typedef struct tda_PQueue tda_PQueue;

/// @name lifetime
/// @{

/// an empty queue that owns no block yet
/// @param elem_size the size of one elem, asserted greater than 0
/// @param cmp the order to serve in, fixed for the life of the queue
/// @param al the allocator, kept for everything after
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the vec cannot be allocated
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_new(size_t elem_size, tda_Cmp cmp, tda_Al *al, tda_PQueue **out);

/// an empty queue with room for 'cap' elems before the first growth
/// @param cap how many elems to make room for
/// @param elem_size the size of one elem, asserted greater than 0
/// @param cmp the order to serve in
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         cap * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_new_cap(size_t cap, size_t elem_size, tda_Cmp cmp, tda_Al *al, tda_PQueue **out);

/// a queue over a copy of 'len' elems read from 'data', in any order
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem_size the size of one elem, asserted greater than 0
/// @param cmp the order to serve in
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem_size overflows
/// @bigo{n} — heapifying in one pass is cheaper than 'len' pushes, which cost O(n log n)
[[nodiscard]] TDA_API
tda_Status tda_pqueue_from_data(
    const void *data, size_t len, size_t elem_size,
    tda_Cmp cmp,
    tda_Al *al,
    tda_PQueue **out
);

/// a queue over a copy of what 's' views, taking its elem_size
/// @param s the view to copy, in any order
/// @param cmp the order to serve in
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_from_span(tda_Span s, tda_Cmp cmp, tda_Al *al, tda_PQueue **out);

/// releases the elems and the queue through the allocator it was built with
/// @param self null is a no-op, so this is safe on a partly built object
/// @bigo{1}
TDA_API
void tda_pqueue_drop(tda_PQueue *self);

/// hands the elems over to the vec that held them and releases the queue around it
/// @param self consumed: its header goes back to the allocator, and the handle must not
///             be used again. Null is not allowed — there would be nothing to hand back
/// @return the vec, elems in HEAP order rather than sorted, with the capacity and the
///         allocator they already had. The comparator stays behind, being the queue's and
///         not the elems'; tda_span_sort_heap over tda_vec_to_span_mut finishes the sort
///         in place. Nothing is copied, so nothing can fail
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Vec *tda_pqueue_into_vec(tda_PQueue *self);

/// @}

/// @name copy
/// @{

/// a new queue with the same elems and comparator, on the same allocator
/// @param self the queue to copy
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n} — the arrangement is copied as it is, so nothing is reheapified
[[nodiscard]] TDA_API
tda_Status tda_pqueue_copy(const tda_PQueue *self, tda_PQueue **out);

/// a new queue with the same elems and comparator, on 'al'
/// @param self the queue to copy
/// @param al where the copy lives; tda_pqueue_copy is this one with the allocator of 'self'
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n} — the arrangement is copied as it is, so nothing is reheapified
[[nodiscard]] TDA_API
tda_Status tda_pqueue_copy_with(const tda_PQueue *self, tda_Al *al, tda_PQueue **out);

/// overwrites the elems of 'other' with those of 'self', growing its block when it must
/// @param self the queue to copy from
/// @param[in,out] other must have the same elem_size; receives the comparator along with
///                      the elems, overwriting its own, and keeps its own allocator.
///                      'self' == 'other' is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'other' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_copy_assign(const tda_PQueue *self, tda_PQueue *other);

/// moves the elems of 'self' into 'other', leaving 'self' empty
/// @param[in,out] self the queue to move from; emptied on success and still usable, on
///                     its own allocator
/// @param[in,out] other must have the same elem_size; receives the comparator along with the elems,
///                      releases what it held and keeps its own allocator. 'self' == 'other' is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and the block cannot be taken,
///         leaving both as they were
/// @bigo{1} on one allocator, n on two — the block belongs to the allocator that made it
[[nodiscard]] TDA_API
tda_Status tda_pqueue_move_assign(tda_PQueue *self, tda_PQueue *other);

/// @}

/// @name info
/// @{

/// how many elems are waiting
/// @param self the queue
/// @return the length, never above the capacity
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_pqueue_len(const tda_PQueue *self);

/// how many elems fit before the block must grow
/// @param self the queue
/// @return the capacity
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_pqueue_cap(const tda_PQueue *self);

/// the size of one elem, as named at construction
/// @param self the queue
/// @return elem_size
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_pqueue_elem_size(const tda_PQueue *self);

/// the allocator the queue was built with
/// @param self the queue
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_pqueue_al(const tda_PQueue *self);

/// the order the queue serves in
/// @param self the queue
/// @return the comparator, as given at construction; it moves only through copy_assign
///         and swap
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Cmp tda_pqueue_cmp(const tda_PQueue *self);

/// @}

/// @name access
/// @{

/// the greatest elem by 'cmp', the one the next pop drops
/// @param self asserts the queue is not empty
/// @return a read-only pointer into the block, good until the next push or pop. There is
///         no mutable form: a write through one would break the heap invariant
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_pqueue_top(const tda_PQueue *self);

/// @}

/// @name mods
/// @{

/// puts a copy of 'val' in, sifting it up to its place
/// @param self the queue
/// @param val must not point into the queue's own elems: a push that grows moves them out
///            from under it
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{log n} — plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_pqueue_push(tda_PQueue *self, const void *val);

/// drops the greatest elem and sifts the next one up
/// @param self asserts the queue is not empty. Read the elem with tda_pqueue_top first —
///             a pop that returned it would have nowhere to put it
/// @bigo{log n}
TDA_API
void tda_pqueue_pop(tda_PQueue *self);

/// drops every elem, keeping the block
/// @param self the queue
/// @bigo{1}
TDA_API
void tda_pqueue_clear(tda_PQueue *self);

/// makes room for 'new_cap' elems
/// @param self the queue
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, or new_cap * elem_size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_reserve(tda_PQueue *self, size_t new_cap);

/// gives back the room above the length
/// @param self a length of 0 releases the block outright
/// @retval TDA_STATUS_OK on success, and when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the allocator refuses the smaller block, leaving
///         the queue as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_shrink_to_fit(tda_PQueue *self);

/// exchanges the contents of the two, comparators and all
/// @param[in,out] self one queue
/// @param[in,out] other must have the same elem_size and the same allocator: the vecs
///                      underneath change hands where they lie, so nothing is copied and
///                      nothing can fail. 'self' == 'other' is a no-op
/// @note two allocators are a broken precondition, not a runtime state: a block belongs to
///       the one that made it. Across two: tda_pqueue_copy_with, then tda_pqueue_move_assign
/// @bigo{1}
TDA_API
void tda_pqueue_swap(tda_PQueue *self, tda_PQueue *other);

/// @}

/// @name to span
/// @{

/// the elems in heap order, which is not sorted order: only the first is in its final
/// place
/// @param self the queue
/// @return a read-only view, good until the next push or pop. Read only because the
///         arrangement is the queue's to keep
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Span tda_pqueue_to_span(const tda_PQueue *self);

/// @}

/// @name print
/// @{

/// writes the elems to a stream in heap order as [a, b, c], followed by a newline
/// @param self the queue
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_pqueue_fprint(const tda_PQueue *self, FILE *stream, tda_FPrint fprint);

/// tda_pqueue_fprint to stdout
/// @param self the queue
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_pqueue_print(const tda_PQueue *self, tda_FPrint fprint);

/// @}

/// @name macros
/// @{

/// tda_pqueue_new with sizeof(T) for the elem size
/// @param T the elem type
/// @param cmp the order to serve in
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{1}
#define TDA_PQUEUE_NEW(T, cmp, al, out) \
    tda_pqueue_new(sizeof(T), (cmp), (al), (out))

/// tda_pqueue_new_cap with sizeof(T)
/// @param T the elem type
/// @param cap how many elems to make room for
/// @param cmp the order to serve in
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{n}
#define TDA_PQUEUE_NEW_CAP(T, cap, cmp, al, out) \
    tda_pqueue_new_cap((cap), sizeof(T), (cmp), (al), (out))

/// tda_pqueue_from_data with sizeof(T)
/// @param T the elem type
/// @param data the elems to copy in, made to typecheck as a const T *
/// @param len how many elems to read from 'data'
/// @param cmp the order to serve in
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{n}
#define TDA_PQUEUE_FROM_DATA(T, data, len, cmp, al, out) \
    tda_pqueue_from_data((const T *){ (data) }, (len), sizeof(T), (cmp), (al), (out))

/// a new queue from the elems written out: TDA_PQUEUE_OF(int32_t, tda_cmp_i32, al, &q, 5, 1)
/// @param T the elem type
/// @param cmp the order to serve in
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @param ... the elems, in any order, as a T initializer list
/// @bigo{n}
#define TDA_PQUEUE_OF(T, cmp, al, out, ...)             \
    tda_pqueue_from_data(                               \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        sizeof(T), (cmp), (al), (out))

/// tda_pqueue_top as a const T *
/// @param T the elem type
/// @param self the queue
/// @bigo{1}
#define TDA_PQUEUE_TOP_AS(T, self) \
    ((const T *) tda_pqueue_top((self)))

/// tda_pqueue_push from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the queue
/// @param val the value to copy in
/// @bigo{log n}
#define TDA_PQUEUE_PUSH(T, self, val) \
    tda_pqueue_push((self), &(T){ (val) })

/// @}

/// @}
