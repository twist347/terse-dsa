#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/check.h"
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
/// Push and pop at one end, the top, in O(1) amortized. A ds/vec holds the elems; a
/// deque's ring would only add a head and a wrap check, since a stack uses one end.
///
/// A narrower vec: no get by index, no insert, no remove, no access to the bottom.
///
/// The order of the elems is the stack's meaning, so algo gets only a read-only view
/// through tda_stack_to_span.
///
/// Pointers are not stable. An op that may reallocate (push, reserve, shrink_to_fit,
/// copy_assign) moves every elem, move_assign releases the old block, and a pop ends the
/// one it takes. swap moves no elem: a pointer stays valid but points into the other
/// one. For stable positions use ds/list.
///
/// @par Example
/// @snippet ds/example_stack.c build
/// @snippet ds/example_stack.c lifo
/// @snippet ds/example_stack.c read
/// @snippet ds/example_stack.c into
/// @{

/// Owning last in first out stack. A value the caller holds, like tda_Vec, whose rules it
/// follows: an init returns it, a from_ fills it in, tda_stack_deinit releases it, `= {}`
/// is inert, and a copy of the struct is a second owner of the same block.
typedef struct tda_Stack {
    tda_Vec vec_; ///< holds the elems, bottom first
} tda_Stack;

/// @name lifetime
/// @{

/// an empty stack that owns no block yet; allocates nothing, so it cannot fail
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al used for every later allocation
/// @return the new stack
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Stack tda_stack_init(tda_Layout elem, tda_Al al);

/// an empty stack with room for 'cap' elems before the first growth
/// @param cap how many elems to make room for
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         cap * elem.size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_init_cap(size_t cap, tda_Layout elem, tda_Al al, tda_Stack *out);

/// a stack holding a copy of 'len' elems read from 'data', pushed in order
/// @param data the elems to copy in; 'data[0]' is the bottom and the last one the top.
///             May be null only when len is 0
/// @param len how many elems to read
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem.size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_from_data(const void *data, size_t len, tda_Layout elem, tda_Al al, tda_Stack *out);

/// a stack holding a copy of the elems 's' views
/// @param s the view to copy; its last elem is the top; the elem layout is the span's
/// @param al the allocator
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_from_span(tda_Span s, tda_Al al, tda_Stack *out);

/// releases what the stack holds and leaves it inert, `= {}`
/// @param self a stack, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_stack_deinit(tda_Stack *self);

/// hands over the vec that holds the elems, leaving the stack inert
/// @param self the stack; inert afterwards
/// @return the vec, elems bottom to top, with the same capacity and allocator;
///         nothing is copied, so this cannot fail
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Vec tda_stack_into_vec(tda_Stack *self);

/// @}

/// @name copy
/// @{

/// a new stack with the same elems in the same order, on the same allocator
/// @param self the stack to copy
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_copy(const tda_Stack *self, tda_Stack *out);

/// a new stack with the same elems in the same order, on 'al'
/// @param self the stack to copy
/// @param al the allocator for the copy
/// @param[out] out the new stack, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_copy_with(const tda_Stack *self, tda_Al al, tda_Stack *out);

/// overwrites the elems of 'self' with those of 'other', growing the block if needed
/// @param[in,out] self must have the same elem layout; keeps its allocator
/// @param other the stack to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'self' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_copy_assign(tda_Stack *self, const tda_Stack *other);

/// releases the elems of 'self' and hands it everything 'other' holds, its allocator
/// included, leaving 'other' empty on that allocator
/// @param[in,out] self must have the same elem layout
/// @param[in,out] other emptied and still usable; may be 'self'
/// @bigo{1}
TDA_API
void tda_stack_move_assign(tda_Stack *self, tda_Stack *other);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems bottom to top, byte for byte
/// @param a one stack
/// @param b must have the same elem layout (asserted); a different length gives false
/// @return whether the lengths match and the bytes do
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_stack_eq(const tda_Stack *a, const tda_Stack *b);

/// whether the two hold equal elems under 'eq'
/// @param a one stack
/// @param b must have the same elem layout
/// @param eq asked of each pair until one fails
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_stack_eq_by(const tda_Stack *a, const tda_Stack *b, tda_Eq eq, void *ctx);

/// @}

/// @name info
/// @{

/// how many elems the stack holds
/// @param self the stack
/// @return the length
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_stack_len(const tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_len(&self->vec_);
}

/// how many elems fit before the block must grow
/// @param self the stack
/// @return the capacity
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_stack_cap(const tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_cap(&self->vec_);
}

/// the layout of one elem, as the stack was built with
/// @param self the stack
/// @return the size and the alignment; every elem is aligned to it
/// @bigo{1}
[[nodiscard]]
static inline tda_Layout tda_stack_elem_layout(const tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_elem_layout(&self->vec_);
}

/// the allocator the stack was built with
/// @param self the stack
/// @return the view it was built with
/// @bigo{1}
[[nodiscard]]
static inline tda_Al tda_stack_al(const tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_al(&self->vec_);
}

/// @}

/// @name access
/// @{

/// the elem that will be popped next
/// @param self asserts the stack is not empty
/// @return valid until the stack next changes
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_stack_top(const tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);
    TDA_EXPECT(tda_vec_len(&self->vec_) > 0);

    return tda_vec_back(&self->vec_);
}

/// the top elem, to write through; writing an elem does not break the stack order
/// @copydetails tda_stack_top
[[nodiscard]]
static inline void *tda_stack_top_mut(tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);
    TDA_EXPECT(tda_vec_len(&self->vec_) > 0);

    return tda_vec_back_mut(&self->vec_);
}

/// @}

/// @name mods
/// @{

/// puts a copy of 'val' on top
/// @param self the stack
/// @param val must not point into this stack: growing would move it
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{1} amortized
[[nodiscard]] TDA_API
tda_Status tda_stack_push(tda_Stack *self, const void *val);

/// drops the top elem, keeping the capacity
/// @param self asserts the stack is not empty; tda_stack_pop_to keeps a copy
/// @bigo{1}
static inline void tda_stack_pop(tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);
    TDA_EXPECT(tda_vec_len(&self->vec_) > 0);

    tda_vec_pop(&self->vec_);
}

/// copies the top elem to 'out', then drops it
/// @param self asserts the stack is not empty
/// @param[out] out where the elem goes
/// @bigo{1}
TDA_API
void tda_stack_pop_to(tda_Stack *self, void *out);

/// drops every elem, keeping the block
/// @param self the stack
/// @bigo{1}
static inline void tda_stack_clear(tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    tda_vec_clear(&self->vec_);
}

/// makes room for 'new_cap' elems
/// @param self the stack
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, or new_cap * elem.size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_reserve(tda_Stack *self, size_t new_cap);

/// gives back the room above the length
/// @param self a length of 0 releases the block
/// @retval TDA_STATUS_OK on success, including when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the smaller block is refused; nothing changes
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_stack_shrink_to_fit(tda_Stack *self);

/// exchanges the contents of two stacks
/// @param[in,out] self one stack
/// @param[in,out] other must have the same elem layout; may be 'self'. Each
///                      side keeps the allocator its elems came from, so the two may differ
/// @bigo{1}
TDA_API
void tda_stack_swap(tda_Stack *self, tda_Stack *other);

/// @}

/// @name to span
/// @{

/// a read-only view of the elems bottom to top, so the top is the last
/// @param self the stack
/// @return valid until the stack next changes; read-only so algo cannot reorder it
/// @bigo{1}
[[nodiscard]]
static inline tda_Span tda_stack_to_span(const tda_Stack *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_to_span(&self->vec_);
}

/// @}

/// @name print
/// @{

/// writes the elems bottom to top as [a, b, c], with no newline
/// @param self the stack
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_stack_fprint(const tda_Stack *self, FILE *stream, tda_FPrint fprint, void *ctx);

/// tda_stack_fprint to stdout, and a newline
/// @param self the stack
/// @param fprint the printer, called once per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_stack_print(const tda_Stack *self, tda_FPrint fprint, void *ctx);

/// @}

/// @name macros
/// @{

/// @cond
// returns 'self' after checking TDA_LAYOUT_OF(T), so a macro evaluates its pointer
// once; _Generic keeps the pointer's constness
[[nodiscard]]
static inline const tda_Stack *tda_stack_typed_(const tda_Stack *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_stack_elem_layout(self), elem));
    return self;
}

[[nodiscard]]
static inline tda_Stack *tda_stack_typed_mut_(tda_Stack *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_stack_elem_layout(self), elem));
    return self;
}

#define TDA_STACK_TYPED_(T, self) \
    _Generic((self), const tda_Stack *: tda_stack_typed_, tda_Stack *: tda_stack_typed_mut_)((self), TDA_LAYOUT_OF(T))
/// @endcond

/// tda_stack_init with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param al the allocator
/// @bigo{1}
#define TDA_STACK_INIT(T, al) \
    tda_stack_init(TDA_LAYOUT_OF(T), (al))

/// tda_stack_init_cap with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param cap how many elems to make room for
/// @param al the allocator
/// @param[out] out where the new stack is written
/// @bigo{n}
#define TDA_STACK_INIT_CAP(T, cap, al, out) \
    tda_stack_init_cap((cap), TDA_LAYOUT_OF(T), (al), (out))

/// tda_stack_from_data with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param data the elems, typechecked as const T *
/// @param len how many to read
/// @param al the allocator
/// @param[out] out where the new stack is written
/// @bigo{n}
#define TDA_STACK_FROM_DATA(T, data, len, al, out) \
    tda_stack_from_data((const T *){ (data) }, (len), TDA_LAYOUT_OF(T), (al), (out))

/// a new stack from the elems written out: TDA_STACK_OF(int32_t, al, &s, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new stack is written
/// @param ... the elems, bottom to top, as a T initializer list, not empty
/// @bigo{n}
#define TDA_STACK_OF(T, al, out, ...)                   \
    tda_stack_from_data(                                \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        TDA_LAYOUT_OF(T), (al), (out))

/// tda_stack_top as a const T *
/// @param T the elem type
/// @param self the stack
/// @bigo{1}
#define TDA_STACK_TOP_AS(T, self) \
    ((const T *) tda_stack_top(TDA_STACK_TYPED_(T, self)))

/// tda_stack_top_mut as a T *
/// @copydetails TDA_STACK_TOP_AS
#define TDA_STACK_TOP_MUT_AS(T, self) \
    ((T *) tda_stack_top_mut(TDA_STACK_TYPED_(T, self)))

/// tda_stack_push from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param self the stack
/// @param val the value to copy in
/// @bigo{1} amortized
#define TDA_STACK_PUSH(T, self, val) \
    tda_stack_push(TDA_STACK_TYPED_(T, self), &(T[1]){ (val) }[0])

/// tda_stack_pop_to with 'out' typechecked as T *
/// @param T the elem type
/// @param self the stack
/// @param[out] out where the elem goes
/// @bigo{1}
#define TDA_STACK_POP_TO(T, self, out) \
    tda_stack_pop_to(TDA_STACK_TYPED_(T, self), (T *){ (out) })

/// @}

/// @}
