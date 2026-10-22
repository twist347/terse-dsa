#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/check.h"
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
/// Push at the back and pop from the front, in O(1) amortized. A ds/deque holds the
/// elems, since both its ends are cheap; over a vec each pop would be O(n).
///
/// A narrower deque: no get by index, no insert, no remove, no push_front or pop_back.
///
/// The order of the elems is the queue's meaning, so algo gets only a copy through
/// tda_queue_copy_to_span; there is no copy_from_span. There is no to_span either, as
/// the elems may wrap around the ring.
///
/// Pointers are not stable. An op that may reallocate (push, reserve, shrink_to_fit,
/// copy_assign, move_assign, swap) moves every elem, and a pop ends the one it takes.
/// For stable positions use ds/list.
///
/// @par Example
/// @snippet ds/example_queue.c build
/// @snippet ds/example_queue.c fifo
/// @snippet ds/example_queue.c read
/// @snippet ds/example_queue.c into
/// @{

/// Owning first in first out queue.
/// Opaque: made by a constructor, released by tda_queue_drop
typedef struct tda_Queue tda_Queue;

/// @name lifetime
/// @{

/// an empty queue that owns no block yet
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al used for every later allocation
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

/// a queue holding a copy of 'len' elems read from 'data', pushed in order
/// @param data 'data[0]' is served first; may be null only when len is 0
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

/// a queue holding a copy of the elems 's' views
/// @param s the view to copy; its first elem is served first
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_from_span(tda_Span s, tda_Al *al, tda_Queue **out);

/// releases the elems and the queue through the allocator it was built with
/// @param self null is a no-op
/// @bigo{1}
TDA_API
void tda_queue_drop(tda_Queue *self);

/// releases the queue and returns the deque that held its elems
/// @param self consumed and must not be used again; not null
/// @return the deque, elems front to back, with the same capacity and allocator; nothing
///         is copied, so this cannot fail
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
/// @param al the allocator for the copy
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_copy_with(const tda_Queue *self, tda_Al *al, tda_Queue **out);

/// overwrites the elems of 'self' with those of 'other', growing the block if needed
/// @param[in,out] self must have the same elem_size; keeps its allocator
/// @param other the queue to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'self' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_copy_assign(tda_Queue *self, const tda_Queue *other);

/// moves the elems of 'other' into 'self', leaving 'other' empty
/// @param[in,out] self must have the same elem_size; its old elems are released, its
///                     allocator kept
/// @param[in,out] other emptied on success and still usable; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the allocators differ and the copy cannot be
///         allocated; both are left unchanged
/// @bigo{1} on one allocator, n across two
[[nodiscard]] TDA_API
tda_Status tda_queue_move_assign(tda_Queue *self, tda_Queue *other);

/// copies every elem into 'dst' front to back; the only way to hand the elems to algo
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
/// @param b must have the same elem_size (asserted); a different length gives false
/// @return whether the lengths match and the bytes do
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_queue_eq(const tda_Queue *a, const tda_Queue *b);

/// whether the two hold equal elems under 'eq'
/// @param a one queue
/// @param b must have the same elem_size
/// @param eq asked of each pair until one fails
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_queue_eq_by(const tda_Queue *a, const tda_Queue *b, tda_Eq eq);

/// @}

/// @name info
/// @{

/// how many elems are waiting
/// @param self the queue
/// @return the length
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_queue_len(const tda_Queue *self);

/// how many elems fit before the block must grow
/// @param self the queue
/// @return the capacity
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_queue_cap(const tda_Queue *self);

/// the size of one elem
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

/// the elem to be served next
/// @param self asserts the queue is not empty
/// @return valid until the queue next changes
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_queue_front(const tda_Queue *self);

/// the front elem, to write through; writing an elem does not break the queue order
/// @copydetails tda_queue_front
[[nodiscard]] TDA_API
void *tda_queue_front_mut(tda_Queue *self);

/// the elem pushed most recently
/// @param self asserts the queue is not empty
/// @return valid until the queue next changes
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

/// pushes a copy of 'val' at the back
/// @param self the queue
/// @param val must not point into this queue: growing would move it
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{1} amortized
[[nodiscard]] TDA_API
tda_Status tda_queue_push(tda_Queue *self, const void *val);

/// drops the front elem, keeping the capacity
/// @param self asserts the queue is not empty; tda_queue_pop_to keeps a copy
/// @bigo{1}
TDA_API
void tda_queue_pop(tda_Queue *self);

/// copies the front elem to 'out', then drops it
/// @param self asserts the queue is not empty
/// @param[out] out where the elem goes
/// @bigo{1}
TDA_API
void tda_queue_pop_to(tda_Queue *self, void *out);

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
/// @param self a length of 0 releases the block
/// @retval TDA_STATUS_OK on success, including when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the smaller block is refused; nothing changes
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_queue_shrink_to_fit(tda_Queue *self);

/// exchanges the contents of two queues
/// @param[in,out] self one queue
/// @param[in,out] other must have the same elem_size and allocator; may be 'self'
/// @note a block belongs to the allocator that made it. Across allocators use
///       tda_queue_copy_with, then tda_queue_move_assign
/// @bigo{1}
TDA_API
void tda_queue_swap(tda_Queue *self, tda_Queue *other);

/// @}

/// @name print
/// @{

/// writes the elems front to back as [a, b, c] and a newline
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

/// @cond
// returns 'self' after checking sizeof(T), so a macro evaluates its handle once;
// _Generic keeps the handle's constness
[[nodiscard]]
static inline const tda_Queue *tda_queue_typed_c_(const tda_Queue *self, [[maybe_unused]] size_t size) {
    TDA_EXPECT(tda_queue_elem_size(self) == size);
    return self;
}

[[nodiscard]]
static inline tda_Queue *tda_queue_typed_(tda_Queue *self, [[maybe_unused]] size_t size) {
    TDA_EXPECT(tda_queue_elem_size(self) == size);
    return self;
}

#define TDA_QUEUE_TYPED_(T, self) \
    _Generic((self), const tda_Queue *: tda_queue_typed_c_, tda_Queue *: tda_queue_typed_)((self), sizeof(T))
/// @endcond

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
/// @param data the elems, typechecked as const T *
/// @param len how many to read
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{n}
#define TDA_QUEUE_FROM_DATA(T, data, len, al, out) \
    tda_queue_from_data((const T *){ (data) }, (len), sizeof(T), (al), (out))

/// a new queue from the elems written out: TDA_QUEUE_OF(int32_t, al, &q, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @param ... the elems, front to back, as a T initializer list, not empty
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
    ((const T *) tda_queue_front(TDA_QUEUE_TYPED_(T, self)))

/// tda_queue_front_mut as a T *
/// @copydetails TDA_QUEUE_FRONT_AS
#define TDA_QUEUE_FRONT_MUT_AS(T, self) \
    ((T *) tda_queue_front_mut(TDA_QUEUE_TYPED_(T, self)))

/// tda_queue_back as a const T *
/// @copydetails TDA_QUEUE_FRONT_AS
#define TDA_QUEUE_BACK_AS(T, self) \
    ((const T *) tda_queue_back(TDA_QUEUE_TYPED_(T, self)))

/// tda_queue_back_mut as a T *
/// @copydetails TDA_QUEUE_FRONT_AS
#define TDA_QUEUE_BACK_MUT_AS(T, self) \
    ((T *) tda_queue_back_mut(TDA_QUEUE_TYPED_(T, self)))

/// tda_queue_push from a value rather than an address
/// @param T the elem type; scalars only, as 'val' becomes a compound literal
/// @param self the queue
/// @param val the value to copy in
/// @bigo{1} amortized
#define TDA_QUEUE_PUSH(T, self, val) \
    tda_queue_push(TDA_QUEUE_TYPED_(T, self), &(T){ (val) })

/// tda_queue_pop_to with 'out' typechecked as T *
/// @param T the elem type
/// @param self the queue
/// @param[out] out where the elem goes
/// @bigo{1}
#define TDA_QUEUE_POP_TO(T, self, out) \
    tda_queue_pop_to(TDA_QUEUE_TYPED_(T, self), (T *){ (out) })

/// @}

/// @}
