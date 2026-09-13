#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/status.h"
#include "tda/ds/deque.h"

#include <stddef.h>

/// @file

/// @defgroup ds_queue ds/queue
/// @ingroup ds
/// @brief tda_Queue — an owning first in first out queue
///
/// Elems join at the back and leave from the front, both in O(1) amortized. A ds/deque
/// holds them: it is the container whose two ends are equally cheap, where over a vec the
/// same type would be O(n) per pop.
///
/// What this adds over the deque it wraps is a NARROWER interface, not an invariant over
/// the elems: no get by index, no insert, no remove, no push_front, no pop_back.
///
/// The order of the elems IS the queue's meaning, so the bridge to algo runs one way
/// only: tda_queue_copy_to_span hands out a copy to read, and there is no copy_from_span
/// to write one back. There is no to_span either — the elems may wrap the ring, so there
/// is no run of bytes to view.
///
/// The pointers are not stable: growing moves every elem. That is what ds/list is for.
///
/// @par Example
/// @snippet ds/example_queue.c build
/// @snippet ds/example_queue.c fifo
/// @snippet ds/example_queue.c read
/// @snippet ds/example_queue.c into
/// @{

/// Owning first in first out queue.
/// An opaque handle: it comes from a constructor and goes back to tda_queue_drop
typedef struct tda_Queue tda_Queue;

/// @name lifetime
/// @{

/// an empty queue that owns no block yet
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator, kept for everything after
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the deque cannot be allocated
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_queue_new(size_t elem_size, tda_Al *al, tda_Queue **out);

/// an empty queue with room for 'cap' elems before the first growth
/// @param cap how many elems to make room for
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         cap * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_new_cap(size_t cap, size_t elem_size, tda_Al *al, tda_Queue **out);

/// a queue holding a copy of 'len' elems read from 'data', joined in the order given
/// @param data the elems to copy in; 'data[0]' is the one served first. May be null only
///             when len is 0
/// @param len how many elems to read
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_Queue **out);

/// a queue holding a copy of what 's' views, taking its elem_size
/// @param s the view to copy; its first elem is served first
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_from_span(tda_Span s, tda_Al *al, tda_Queue **out);

/// releases the elems and the queue through the allocator it was built with
/// @param self null is a no-op, so this is safe on a partly built object
/// @bigo{1}
TDA_API
void tda_queue_drop(tda_Queue *self);

/// hands the elems over to the deque that held them and releases the queue around it
/// @param self consumed: its header goes back to the allocator, and the handle must not
///             be used again. Null is not allowed — there would be nothing to hand back
/// @return the deque, holding the elems front to back with the capacity and the allocator
///         they already had. Nothing is copied, so nothing can fail
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Deque *tda_queue_into_deque(tda_Queue *self);

/// @}

/// @name copy
/// @{

/// a new queue with the same elems in the same order, on the same allocator
/// @param self the queue to copy
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_copy(const tda_Queue *self, tda_Queue **out);

/// a new queue with the same elems in the same order, on 'al'
/// @param self the queue to copy
/// @param al where the copy lives; tda_queue_copy is this one with the allocator of 'self'
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_copy_with(const tda_Queue *self, tda_Al *al, tda_Queue **out);

/// overwrites the elems of 'other' with those of 'self', growing its block when it must
/// @param self the queue to copy from
/// @param[in,out] other must have the same elem_size; keeps its own allocator, and
///                      'self' == 'other' is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'other' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_copy_assign(const tda_Queue *self, tda_Queue *other);

/// moves the elems of 'self' into 'other', leaving 'self' empty
/// @param[in,out] self the queue to move from; emptied on success and still usable, on
///                     its own allocator
/// @param[in,out] other must have the same elem_size; releases what it held and keeps
///                      its own allocator. 'self' == 'other' is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and the block cannot be taken,
///         leaving both as they were
/// @bigo{1} on one allocator, n on two — the block belongs to the allocator that made it
[[nodiscard]] TDA_API
tda_Status tda_queue_move_assign(tda_Queue *self, tda_Queue *other);

/// writes every elem into 'dst' front to back. The whole bridge to algo, and read only by
/// design — see the note on the type
/// @param self the queue
/// @param dst must have the same elem_size and be exactly as long as the queue
/// @bigo{n}
TDA_API
void tda_queue_copy_to_span(const tda_Queue *self, tda_SpanMut dst);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems in queue order, byte for byte
/// @param a one queue
/// @param b must have the same elem_size — a mismatch there is a programmer error, not a
///          false; a differing length is just false
/// @return whether the lengths match and the bytes do
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_queue_eq(const tda_Queue *a, const tda_Queue *b);

/// whether the two hold equal elems under 'eq'
/// @param a one queue
/// @param b must have the same elem_size as 'a'
/// @param eq asked of every pair until one says no
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_queue_eq_by(const tda_Queue *a, const tda_Queue *b, tda_Eq eq);

/// @}

/// @name info
/// @{

/// how many elems are waiting
/// @param self the queue
/// @return the length, never above the capacity
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_queue_len(const tda_Queue *self);

/// how many elems fit before the block must grow
/// @param self the queue
/// @return the capacity
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_queue_cap(const tda_Queue *self);

/// the size of one elem, as named at construction
/// @param self the queue
/// @return elem_size
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_queue_elem_size(const tda_Queue *self);

/// the allocator the queue was built with
/// @param self the queue
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_queue_al(const tda_Queue *self);

/// @}

/// @name access
/// @{

/// the elem to be served next. Named front rather than first because a queue names roles,
/// not places in a sequence
/// @param self asserts the queue is not empty
/// @return a pointer into the block, good until the next push
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_queue_front(const tda_Queue *self);

/// the front elem, to write through — what makes a queue a queue is where elems enter and
/// leave, not what they hold
/// @copydetails tda_queue_front
[[nodiscard]] TDA_API
void *tda_queue_front_mut(tda_Queue *self);

/// the elem that arrived most recently
/// @param self asserts the queue is not empty
/// @return a pointer into the block, good until the next push
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_queue_back(const tda_Queue *self);

/// the back elem, to write through
/// @copydetails tda_queue_back
[[nodiscard]] TDA_API
void *tda_queue_back_mut(tda_Queue *self);

/// @}

/// @name mods
/// @{

/// joins a copy of 'val' to the back
/// @param self the queue
/// @param val must not point into the queue's own elems: a push that grows moves them out
///            from under it
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{1} amortized
[[nodiscard]] TDA_API
tda_Status tda_queue_push(tda_Queue *self, const void *val);

/// drops the front elem, keeping the capacity
/// @param self asserts the queue is not empty. Read the elem with tda_queue_front first —
///             a pop that returned it would have nowhere to put it
/// @bigo{1}
TDA_API
void tda_queue_pop(tda_Queue *self);

/// drops every elem, keeping the block
/// @param self the queue
/// @bigo{1}
TDA_API
void tda_queue_clear(tda_Queue *self);

/// makes room for 'new_cap' elems
/// @param self the queue
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, or new_cap * elem_size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_reserve(tda_Queue *self, size_t new_cap);

/// gives back the room above the length
/// @param self a length of 0 releases the block outright
/// @retval TDA_STATUS_OK on success, and when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the allocator refuses the smaller block, leaving
///         the queue as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_shrink_to_fit(tda_Queue *self);

/// exchanges the contents of the two
/// @param[in,out] self one queue
/// @param[in,out] other must have the same elem_size and the same allocator: the deques
///                      underneath change hands where they lie, so nothing is copied and
///                      nothing can fail. 'self' == 'other' is a no-op
/// @note two allocators are a broken precondition, not a runtime state: a block belongs to
///       the one that made it. Across two: tda_queue_copy_with, then tda_queue_move_assign
/// @bigo{1}
TDA_API
void tda_queue_swap(tda_Queue *self, tda_Queue *other);

/// @}

/// @name print
/// @{

/// writes the elems to a stream front to back as [a, b, c], followed by a newline
/// @param self the queue
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_queue_fprint(const tda_Queue *self, FILE *stream, tda_FPrint fprint);

/// tda_queue_fprint to stdout
/// @param self the queue
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_queue_print(const tda_Queue *self, tda_FPrint fprint);

/// @}

/// @name macros
/// @{

/// tda_queue_new with sizeof(T) for the elem size
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{1}
#define TDA_QUEUE_NEW(T, al, out) \
    tda_queue_new(sizeof(T), (al), (out))

/// tda_queue_new_cap with sizeof(T)
/// @param T the elem type
/// @param cap how many elems to make room for
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{n}
#define TDA_QUEUE_NEW_CAP(T, cap, al, out) \
    tda_queue_new_cap((cap), sizeof(T), (al), (out))

/// tda_queue_from_data with sizeof(T)
/// @param T the elem type
/// @param data the elems to copy in, made to typecheck as a const T *
/// @param len how many elems to read from 'data'
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{n}
#define TDA_QUEUE_FROM_DATA(T, data, len, al, out) \
    tda_queue_from_data((const T *){ (data) }, (len), sizeof(T), (al), (out))

/// a new queue from the elems written out: TDA_QUEUE_OF(int32_t, al, &q, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @param ... the elems, front to back, as a T initializer list
/// @bigo{n}
#define TDA_QUEUE_OF(T, al, out, ...)                   \
    tda_queue_from_data(                                \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        sizeof(T), (al), (out))

/// tda_queue_front as a const T *
/// @param T the elem type
/// @param self the queue
/// @bigo{1}
#define TDA_QUEUE_FRONT_AS(T, self) \
    ((const T *) tda_queue_front((self)))

/// tda_queue_front_mut as a T *
/// @copydetails TDA_QUEUE_FRONT_AS
#define TDA_QUEUE_FRONT_MUT_AS(T, self) \
    ((T *) tda_queue_front_mut((self)))

/// tda_queue_back as a const T *
/// @copydetails TDA_QUEUE_FRONT_AS
#define TDA_QUEUE_BACK_AS(T, self) \
    ((const T *) tda_queue_back((self)))

/// tda_queue_back_mut as a T *
/// @copydetails TDA_QUEUE_FRONT_AS
#define TDA_QUEUE_BACK_MUT_AS(T, self) \
    ((T *) tda_queue_back_mut((self)))

/// tda_queue_push from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the queue
/// @param val the value to copy in
/// @bigo{1} amortized
#define TDA_QUEUE_PUSH(T, self, val) \
    tda_queue_push((self), &(T){ (val) })

/// @}

/// @}
