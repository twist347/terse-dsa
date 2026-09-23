#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/status.h"

#include <stddef.h>

/// @file

/// @defgroup ds_deque ds/deque
/// @ingroup ds
/// @brief tda_Deque — an owning queue with two cheap ends
///
/// A growable ring buffer over one block: both ends cost O(1) amortized and get by index
/// stays O(1), since the elem for 'idx' sits at '(head + idx) % cap'.
///
/// The contents may therefore WRAP: they are one run of elems in ring order, not one run
/// of bytes. Two things follow, both absent on purpose.
///
/// There is no tda_deque_data: there is no single block to point at. There is no
/// tda_deque_to_span either, and no linearize to earn one — making the ring contiguous
/// would be a mutation the next push_front undoes. The bridge to algo is the pair
/// tda_deque_copy_to_span / tda_deque_copy_from_span, which copies instead of rearranging.
///
/// The pointers are not stable: growing moves every elem. That is what ds/list is for.
///
/// An index out of range asserts; the ops that return a tda_Status are the ones that
/// allocate.
///
/// @par Example
/// @snippet ds/example_deque.c build
/// @snippet ds/example_deque.c ends
/// @snippet ds/example_deque.c index
/// @snippet ds/example_deque.c algo
/// @{

/// Owning queue with two cheap ends.
/// An opaque handle: it comes from a constructor and goes back to tda_deque_drop
typedef struct tda_Deque tda_Deque;

/// @name lifetime
/// @{

/// an empty deque that owns no block yet
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator, kept for everything after
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header cannot be allocated
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_deque_new(size_t elem_size, tda_Al *al, tda_Deque **out);

/// a deque of 'len' zeroed elems, ready to be written through get_mut or set
/// @param len how many elems
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated, or
///         len * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_new_len(size_t len, size_t elem_size, tda_Al *al, tda_Deque **out);

/// an empty deque with room for 'cap' elems before the first growth
/// @param cap how many elems to make room for
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated, or
///         cap * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_new_cap(size_t cap, size_t elem_size, tda_Al *al, tda_Deque **out);

/// a deque holding a copy of 'len' elems read from 'data', front to back
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new deque, written only on success; its ring starts out unwrapped
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated, or
///         len * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_Deque **out);

/// a deque holding a copy of what 's' views, taking its elem_size
/// @param s the view to copy
/// @param al the allocator
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_from_span(tda_Span s, tda_Al *al, tda_Deque **out);

/// releases the block and the deque through the allocator it was built with
/// @param self null is a no-op, so this is safe on a partly built object
/// @bigo{1}
TDA_API
void tda_deque_drop(tda_Deque *self);

/// @}

/// @name copy
/// @{

/// a new deque with the same elems in the same order, on the same allocator
/// @param self the deque to copy
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_copy(const tda_Deque *self, tda_Deque **out);

/// a new deque with the same elems in the same order, on 'al'
/// @param self the deque to copy
/// @param al where the copy lives; tda_deque_copy is this one with the allocator of 'self'
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_copy_with(const tda_Deque *self, tda_Al *al, tda_Deque **out);

/// overwrites the elems of 'self' with those of 'other', growing its block when it must
/// @param[in,out] self must have the same elem_size; keeps its own allocator, and
///                     'self' == 'other' is a no-op
/// @param other the deque to copy from
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'self' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_copy_assign(tda_Deque *self, const tda_Deque *other);

/// moves the elems of 'other' into 'self', leaving 'other' empty
/// @param[in,out] self must have the same elem_size; releases what it held and keeps
///                     its own allocator. 'self' == 'other' is a no-op
/// @param[in,out] other the deque to move from; emptied on success and still usable, on
///                      its own allocator
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and the block cannot be taken,
///         leaving both as they were
/// @bigo{1} on one allocator, n on two — the block belongs to the allocator that made it
[[nodiscard]] TDA_API
tda_Status tda_deque_move_assign(tda_Deque *self, tda_Deque *other);

/// writes every elem into 'dst' in ring order — at most two memcpy, the contents being at
/// most two runs. This is how a deque reaches algo: sort or search the copy, not the ring
/// @param self the deque
/// @param dst must have the same elem_size and be exactly as long as the deque
/// @bigo{n}
TDA_API
void tda_deque_copy_to_span(const tda_Deque *self, tda_SpanMut dst);

/// overwrites every elem from 'src' — the pair to tda_deque_copy_to_span: take the
/// contents out, hand them to algo, put the answer back
/// @param self the deque
/// @param src must have the same elem_size and be exactly as long as the deque. Nothing
///            is allocated, so nothing can fail
/// @bigo{n}
TDA_API
void tda_deque_copy_from_span(tda_Deque *self, tda_Span src);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems in ring order, byte for byte
/// @param a one deque
/// @param b must have the same elem_size — a mismatch there is a programmer error, not a
///          false; a differing length is just false
/// @return whether the lengths match and the elems do, wherever either ring starts
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_deque_eq(const tda_Deque *a, const tda_Deque *b);

/// whether the two hold equal elems under 'eq'
/// @param a one deque
/// @param b must have the same elem_size as 'a'
/// @param eq asked of every pair until one says no
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_deque_eq_by(const tda_Deque *a, const tda_Deque *b, tda_Eq eq);

/// @}

/// @name info
/// @{

/// how many elems the deque holds
/// @param self the deque
/// @return the length, never above the capacity
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_deque_len(const tda_Deque *self);

/// how many elems fit before the block must grow
/// @param self the deque
/// @return the capacity; it says nothing about where the ring starts
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_deque_cap(const tda_Deque *self);

/// the size of one elem, as named at construction
/// @param self the deque
/// @return elem_size
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_deque_elem_size(const tda_Deque *self);

/// how many bytes the elems take
/// @param self the deque
/// @return len * elem_size, the capacity not counted
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_deque_bytes(const tda_Deque *self);

/// the allocator the deque was built with
/// @param self the deque
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_deque_al(const tda_Deque *self);

/// @}

/// @name access
/// @{

/// the front elem
/// @param self asserts the deque is not empty
/// @return a pointer into the block, good until the next op that may reallocate
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_deque_front(const tda_Deque *self);

/// the front elem, to write through
/// @copydetails tda_deque_front
[[nodiscard]] TDA_API
void *tda_deque_front_mut(tda_Deque *self);

/// the back elem
/// @copydetails tda_deque_front
[[nodiscard]] TDA_API
const void *tda_deque_back(const tda_Deque *self);

/// the back elem, to write through
/// @copydetails tda_deque_front
[[nodiscard]] TDA_API
void *tda_deque_back_mut(tda_Deque *self);

/// the elem at 'idx'
/// @param self the deque
/// @param idx counts from the front, so 0 is the front elem wherever the ring starts;
///            asserts idx < len
/// @return a pointer into the block, good until the next op that may reallocate
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_deque_get(const tda_Deque *self, size_t idx);

/// the elem at 'idx', to write through
/// @copydetails tda_deque_get
[[nodiscard]] TDA_API
void *tda_deque_get_mut(tda_Deque *self, size_t idx);

/// overwrites the elem at 'idx' with a copy of 'val'
/// @param self the deque
/// @param idx counts from the front; asserts idx < len
/// @param val the elem to copy in
/// @bigo{1}
TDA_API
void tda_deque_set(tda_Deque *self, size_t idx, const void *val);

/// @}

/// @name mods
/// @{

/// puts a copy of 'val' at the front
/// @param self the deque
/// @param val must not point into this deque's own block: a push that grows moves the
///            elems out from under it
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{1} amortized
[[nodiscard]] TDA_API
tda_Status tda_deque_push_front(tda_Deque *self, const void *val);

/// puts a copy of 'val' at the back
/// @copydetails tda_deque_push_front
[[nodiscard]] TDA_API
tda_Status tda_deque_push_back(tda_Deque *self, const void *val);

/// drops the front elem, keeping the capacity
/// @param self asserts the deque is not empty
/// @bigo{1}
TDA_API
void tda_deque_pop_front(tda_Deque *self);

/// drops the back elem, keeping the capacity
/// @copydetails tda_deque_pop_front
TDA_API
void tda_deque_pop_back(tda_Deque *self);

/// puts a copy of 'val' at 'idx', shifting whichever side is shorter
/// @param self the deque
/// @param idx asserts idx <= len; idx == len is push_back
/// @param val must not point into this deque's own block, as in tda_deque_push_front
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n} — half the constant of a vec, but still O(n): the two ends are what the type
///            is for
[[nodiscard]] TDA_API
tda_Status tda_deque_insert(tda_Deque *self, size_t idx, const void *val);

/// drops the elem at 'idx', closing the gap from whichever side is shorter
/// @param self the deque
/// @param idx asserts idx < len
/// @bigo{n}
TDA_API
void tda_deque_remove(tda_Deque *self, size_t idx);

/// drops every elem, keeping the block
/// @param self the deque
/// @bigo{1}
TDA_API
void tda_deque_clear(tda_Deque *self);

/// makes room for 'new_cap' elems, unrolling the ring into the new block
/// @param self the deque
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, or new_cap * elem_size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_reserve(tda_Deque *self, size_t new_cap);

/// gives back the room above the length
/// @param self a length of 0 releases the block outright
/// @retval TDA_STATUS_OK on success, and when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the allocator refuses the smaller block, leaving
///         the deque as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_shrink_to_fit(tda_Deque *self);

/// moves the length to 'new_len' at the BACK, so the front stays put and a resize never
/// renumbers what was already there
/// @param self the deque
/// @param new_len below the length drops from the back; above it appends zeroed elems
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_resize(tda_Deque *self, size_t new_len);

/// exchanges the two deques whole, rings and all
/// @param[in,out] self one deque
/// @param[in,out] other must have the same elem_size and the same allocator: the blocks
///                      change hands where they lie, ring and all, so nothing is copied and
///                      nothing can fail. 'self' == 'other' is a no-op
/// @note two allocators are a broken precondition, not a runtime state: a block belongs to
///       the one that made it. Across two: tda_deque_copy_with, then tda_deque_move_assign
/// @bigo{1}
TDA_API
void tda_deque_swap(tda_Deque *self, tda_Deque *other);

/// exchanges the elems at 'i' and 'j'
/// @param self the deque
/// @param i counts from the front; asserts i < len
/// @param j counts from the front; asserts j < len; i == j is a no-op
/// @bigo{1}
TDA_API
void tda_deque_swap_elems(tda_Deque *self, size_t i, size_t j);

/// @}

/// @name print
/// @{

/// writes the elems to a stream in ring order as [a, b, c], followed by a newline
/// @param self the deque
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_deque_fprint(const tda_Deque *self, FILE *stream, tda_FPrint fprint);

/// tda_deque_fprint to stdout
/// @param self the deque
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_deque_print(const tda_Deque *self, tda_FPrint fprint);

/// @}

/// @name macros
/// @{

/// tda_deque_new with sizeof(T) for the elem size
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @bigo{1}
#define TDA_DEQUE_NEW(T, al, out) \
    tda_deque_new(sizeof(T), (al), (out))

/// tda_deque_new_len with sizeof(T)
/// @param T the elem type
/// @param len how many zeroed elems
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @bigo{n}
#define TDA_DEQUE_NEW_LEN(T, len, al, out) \
    tda_deque_new_len((len), sizeof(T), (al), (out))

/// tda_deque_new_cap with sizeof(T)
/// @param T the elem type
/// @param cap how many elems to make room for
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @bigo{n}
#define TDA_DEQUE_NEW_CAP(T, cap, al, out) \
    tda_deque_new_cap((cap), sizeof(T), (al), (out))

/// tda_deque_from_data with sizeof(T)
/// @param T the elem type
/// @param data the elems to copy in, made to typecheck as a const T *
/// @param len how many elems to read from 'data'
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @bigo{n}
#define TDA_DEQUE_FROM_DATA(T, data, len, al, out) \
    tda_deque_from_data((const T *){ (data) }, (len), sizeof(T), (al), (out))

/// a new deque from the elems written out: TDA_DEQUE_OF(int32_t, al, &d, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @param ... the elems, front to back, as a T initializer list
/// @bigo{n}
#define TDA_DEQUE_OF(T, al, out, ...)                   \
    tda_deque_from_data(                                \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        sizeof(T), (al), (out))

/// tda_deque_front as a const T *
/// @param T the elem type
/// @param self the deque
/// @bigo{1}
#define TDA_DEQUE_FRONT_AS(T, self) \
    ((const T *) tda_deque_front((self)))

/// tda_deque_front_mut as a T *
/// @copydetails TDA_DEQUE_FRONT_AS
#define TDA_DEQUE_FRONT_MUT_AS(T, self) \
    ((T *) tda_deque_front_mut((self)))

/// tda_deque_back as a const T *
/// @copydetails TDA_DEQUE_FRONT_AS
#define TDA_DEQUE_BACK_AS(T, self) \
    ((const T *) tda_deque_back((self)))

/// tda_deque_back_mut as a T *
/// @copydetails TDA_DEQUE_FRONT_AS
#define TDA_DEQUE_BACK_MUT_AS(T, self) \
    ((T *) tda_deque_back_mut((self)))

/// walks the deque front to back, binding 'elem' to each elem in turn. A ring has no
/// view to hand out, so this is the walk that copies nothing
/// @param T the elem type
/// @param elem the name the loop variable takes; it is a const T *
/// @param self the deque
/// @note the length is read on every step, so pushing or popping inside the body changes
///       what the walk covers — and a growth moves the elems out from under 'elem'
/// @bigo{n} over the whole walk
#define TDA_DEQUE_FOR_EACH_AS(T, elem, self)                                       \
    for (size_t idx_ = 0, step_ = 0; idx_ < tda_deque_len(self); ++idx_, step_ = 0) \
        for (const T *elem = TDA_DEQUE_GET_AS(T, (self), idx_); !step_; step_ = 1)

/// the same walk over elems that may be written through
/// @copydetails TDA_DEQUE_FOR_EACH_AS
#define TDA_DEQUE_FOR_EACH_MUT_AS(T, elem, self)                                   \
    for (size_t idx_ = 0, step_ = 0; idx_ < tda_deque_len(self); ++idx_, step_ = 0) \
        for (T *elem = TDA_DEQUE_GET_MUT_AS(T, (self), idx_); !step_; step_ = 1)

/// tda_deque_get as a const T *
/// @param T the elem type
/// @param self the deque
/// @param idx the index, counted from the front
/// @bigo{1}
#define TDA_DEQUE_GET_AS(T, self, idx) \
    ((const T *) tda_deque_get((self), (idx)))

/// tda_deque_get_mut as a T *
/// @copydetails TDA_DEQUE_GET_AS
#define TDA_DEQUE_GET_MUT_AS(T, self, idx) \
    ((T *) tda_deque_get_mut((self), (idx)))

/// tda_deque_set from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the deque
/// @param idx the index, counted from the front
/// @param val the value to copy in
/// @bigo{1}
#define TDA_DEQUE_SET(T, self, idx, val) \
    tda_deque_set((self), (idx), &(T){ (val) })

/// tda_deque_push_front from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the deque
/// @param val the value to copy in
/// @bigo{1} amortized
#define TDA_DEQUE_PUSH_FRONT(T, self, val) \
    tda_deque_push_front((self), &(T){ (val) })

/// tda_deque_push_back from a value rather than an address
/// @copydetails TDA_DEQUE_PUSH_FRONT
#define TDA_DEQUE_PUSH_BACK(T, self, val) \
    tda_deque_push_back((self), &(T){ (val) })

/// tda_deque_insert from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the deque
/// @param idx the index, counted from the front
/// @param val the value to copy in
/// @bigo{n}
#define TDA_DEQUE_INSERT(T, self, idx, val) \
    tda_deque_insert((self), (idx), &(T){ (val) })

/// @}

/// @}
