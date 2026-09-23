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

/// @defgroup ds_stack ds/stack
/// @ingroup ds
/// @brief tda_Stack — an owning last in first out stack
///
/// Elems are pushed and popped at one end, the top, both in O(1) amortized. A ds/vec
/// holds them: a stack touches one end only, so the ring of a deque would cost it a head
/// to keep and a wrap to check on every access.
///
/// What this adds over the vec it wraps is a NARROWER interface, not an invariant over
/// the elems: no get by index, no insert, no remove, no reaching the bottom.
///
/// The order of the elems IS the stack's meaning, so the bridge to algo runs one way
/// only: tda_stack_to_span is a view to read, and there is no mutable one to sort through.
///
/// The pointers are not stable: growing moves every elem. That is what ds/list is for.
///
/// @par Example
/// @snippet ds/example_stack.c build
/// @snippet ds/example_stack.c lifo
/// @snippet ds/example_stack.c read
/// @snippet ds/example_stack.c into
/// @{

/// Owning last in first out stack.
/// An opaque handle: it comes from a constructor and goes back to tda_stack_drop
typedef struct tda_Stack tda_Stack;

/// @name lifetime
/// @{

/// an empty stack that owns no block yet
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator, kept for everything after
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the vec cannot be allocated
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_stack_new(size_t elem_size, tda_Al *al, tda_Stack **out);

/// an empty stack with room for 'cap' elems before the first growth
/// @param cap how many elems to make room for
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         cap * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_new_cap(size_t cap, size_t elem_size, tda_Al *al, tda_Stack **out);

/// a stack holding a copy of 'len' elems read from 'data', pushed in the order given
/// @param data the elems to copy in; 'data[0]' ends up at the bottom and the LAST one on
///             top. May be null only when len is 0
/// @param len how many elems to read
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_Stack **out);

/// a stack holding a copy of what 's' views, taking its elem_size
/// @param s the view to copy; its last elem ends up on top
/// @param al the allocator
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_from_span(tda_Span s, tda_Al *al, tda_Stack **out);

/// releases the elems and the stack through the allocator it was built with
/// @param self null is a no-op, so this is safe on a partly built object
/// @bigo{1}
TDA_API
void tda_stack_drop(tda_Stack *self);

/// hands the elems over to the vec that held them and releases the stack around it
/// @param self consumed: its header goes back to the allocator, and the handle must not
///             be used again. Null is not allowed — there would be nothing to hand back
/// @return the vec, holding the elems bottom to top with the capacity and the allocator
///         they already had. Nothing is copied, so nothing can fail
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Vec *tda_stack_into_vec(tda_Stack *self);

/// @}

/// @name copy
/// @{

/// a new stack with the same elems in the same order, on the same allocator
/// @param self the stack to copy
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_copy(const tda_Stack *self, tda_Stack **out);

/// a new stack with the same elems in the same order, on 'al'
/// @param self the stack to copy
/// @param al where the copy lives; tda_stack_copy is this one with the allocator of 'self'
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_copy_with(const tda_Stack *self, tda_Al *al, tda_Stack **out);

/// overwrites the elems of 'self' with those of 'other', growing its block when it must
/// @param[in,out] self must have the same elem_size; keeps its own allocator, and
///                     'self' == 'other' is a no-op
/// @param other the stack to copy from
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'self' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_copy_assign(tda_Stack *self, const tda_Stack *other);

/// moves the elems of 'other' into 'self', leaving 'other' empty
/// @param[in,out] self must have the same elem_size; releases what it held and keeps
///                     its own allocator. 'self' == 'other' is a no-op
/// @param[in,out] other the stack to move from; emptied on success and still usable, on
///                      its own allocator
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and the block cannot be taken,
///         leaving both as they were
/// @bigo{1} on one allocator, n on two — the block belongs to the allocator that made it
[[nodiscard]] TDA_API
tda_Status tda_stack_move_assign(tda_Stack *self, tda_Stack *other);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems bottom to top, byte for byte
/// @param a one stack
/// @param b must have the same elem_size — a mismatch there is a programmer error, not a
///          false; a differing length is just false
/// @return whether the lengths match and the bytes do
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_stack_eq(const tda_Stack *a, const tda_Stack *b);

/// whether the two hold equal elems under 'eq'
/// @param a one stack
/// @param b must have the same elem_size as 'a'
/// @param eq asked of every pair until one says no
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_stack_eq_by(const tda_Stack *a, const tda_Stack *b, tda_Eq eq);

/// @}

/// @name info
/// @{

/// how many elems the stack holds
/// @param self the stack
/// @return the length, never above the capacity
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_stack_len(const tda_Stack *self);

/// how many elems fit before the block must grow
/// @param self the stack
/// @return the capacity
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_stack_cap(const tda_Stack *self);

/// the size of one elem, as named at construction
/// @param self the stack
/// @return elem_size
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_stack_elem_size(const tda_Stack *self);

/// the allocator the stack was built with
/// @param self the stack
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_stack_al(const tda_Stack *self);

/// @}

/// @name access
/// @{

/// the elem that will be popped next
/// @param self asserts the stack is not empty
/// @return a pointer into the block, good until the next push
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_stack_top(const tda_Stack *self);

/// the top elem, to write through — what makes a stack a stack is where elems enter and
/// leave, not what they hold
/// @copydetails tda_stack_top
[[nodiscard]] TDA_API
void *tda_stack_top_mut(tda_Stack *self);

/// @}

/// @name mods
/// @{

/// puts a copy of 'val' on top
/// @param self the stack
/// @param val must not point into the stack's own elems: a push that grows moves them out
///            from under it
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{1} amortized
[[nodiscard]] TDA_API
tda_Status tda_stack_push(tda_Stack *self, const void *val);

/// drops the top elem, keeping the capacity
/// @param self asserts the stack is not empty. Read the elem with tda_stack_top first —
///             a pop that returned it would have nowhere to put it
/// @bigo{1}
TDA_API
void tda_stack_pop(tda_Stack *self);

/// drops every elem, keeping the block
/// @param self the stack
/// @bigo{1}
TDA_API
void tda_stack_clear(tda_Stack *self);

/// makes room for 'new_cap' elems
/// @param self the stack
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, or new_cap * elem_size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_reserve(tda_Stack *self, size_t new_cap);

/// gives back the room above the length
/// @param self a length of 0 releases the block outright
/// @retval TDA_STATUS_OK on success, and when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the allocator refuses the smaller block, leaving
///         the stack as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_shrink_to_fit(tda_Stack *self);

/// exchanges the contents of the two
/// @param[in,out] self one stack
/// @param[in,out] other must have the same elem_size and the same allocator: the vecs
///                      underneath change hands where they lie, so nothing is copied and
///                      nothing can fail. 'self' == 'other' is a no-op
/// @note two allocators are a broken precondition, not a runtime state: a block belongs to
///       the one that made it. Across two: tda_stack_copy_with, then tda_stack_move_assign
/// @bigo{1}
TDA_API
void tda_stack_swap(tda_Stack *self, tda_Stack *other);

/// @}

/// @name to span
/// @{

/// the elems bottom to top, so the top is the LAST of them
/// @param self the stack
/// @return a read-only view, good until the next push. Read only because the arrival
///         order is the stack's to keep — this is the whole bridge to algo
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Span tda_stack_to_span(const tda_Stack *self);

/// @}

/// @name print
/// @{

/// writes the elems to a stream bottom to top as [a, b, c], followed by a newline
/// @param self the stack
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_stack_fprint(const tda_Stack *self, FILE *stream, tda_FPrint fprint);

/// tda_stack_fprint to stdout
/// @param self the stack
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_stack_print(const tda_Stack *self, tda_FPrint fprint);

/// @}

/// @name macros
/// @{

/// tda_stack_new with sizeof(T) for the elem size
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new stack is written
/// @bigo{1}
#define TDA_STACK_NEW(T, al, out) \
    tda_stack_new(sizeof(T), (al), (out))

/// tda_stack_new_cap with sizeof(T)
/// @param T the elem type
/// @param cap how many elems to make room for
/// @param al the allocator
/// @param[out] out where the new stack is written
/// @bigo{n}
#define TDA_STACK_NEW_CAP(T, cap, al, out) \
    tda_stack_new_cap((cap), sizeof(T), (al), (out))

/// tda_stack_from_data with sizeof(T)
/// @param T the elem type
/// @param data the elems to copy in, made to typecheck as a const T *
/// @param len how many elems to read from 'data'
/// @param al the allocator
/// @param[out] out where the new stack is written
/// @bigo{n}
#define TDA_STACK_FROM_DATA(T, data, len, al, out) \
    tda_stack_from_data((const T *){ (data) }, (len), sizeof(T), (al), (out))

/// a new stack from the elems written out: TDA_STACK_OF(int32_t, al, &s, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new stack is written
/// @param ... the elems, bottom to top, as a T initializer list
/// @bigo{n}
#define TDA_STACK_OF(T, al, out, ...)                   \
    tda_stack_from_data(                                \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        sizeof(T), (al), (out))

/// tda_stack_top as a const T *
/// @param T the elem type
/// @param self the stack
/// @bigo{1}
#define TDA_STACK_TOP_AS(T, self) \
    ((const T *) tda_stack_top((self)))

/// tda_stack_top_mut as a T *
/// @copydetails TDA_STACK_TOP_AS
#define TDA_STACK_TOP_MUT_AS(T, self) \
    ((T *) tda_stack_top_mut((self)))

/// tda_stack_push from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the stack
/// @param val the value to copy in
/// @bigo{1} amortized
#define TDA_STACK_PUSH(T, self, val) \
    tda_stack_push((self), &(T){ (val) })

/// @}

/// @}
