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

/// @defgroup ds_pqueue ds/pqueue
/// @ingroup ds
/// @brief tda_PQueue — an owning queue that serves the greatest elem first
///
/// A ds/vec kept as a heap (algo/heap), so the greatest elem by 'cmp' is at the front.
/// For a min-queue, build it with a descending comparator such as tda_cmp_desc_i32;
/// there is no separate type.
///
/// The comparator is set at construction and travels with the elems: copy_assign,
/// move_assign and swap hand it over along with them, since a heap is only valid under
/// the order it was built with, and nothing else changes it.
///
/// There is no tda_pqueue_eq: the same elems pushed in another order form another heap,
/// so a real answer would have to sort a copy. Compare two queues by draining them.
///
/// No elem is handed out to write through (no top_mut, get or to_span_mut): a write
/// could break the heap invariant without the queue noticing.
///
/// Pointers are not stable: every push and pop rearranges the heap, push, extend,
/// reserve, shrink_to_fit and copy_assign may reallocate, moving every elem, and
/// move_assign releases the old block. swap moves no elem: a pointer stays valid but points into
/// the other queue.
///
/// @par Example
/// @snippet ds/example_pqueue.c build
/// @snippet ds/example_pqueue.c serve
/// @snippet ds/example_pqueue.c order
/// @snippet ds/example_pqueue.c into
/// @{

/// Owning queue that serves the greatest elem first. A value the caller holds, like
/// tda_Vec, whose rules it follows: an init that cannot fail returns it, one that can and
/// a from_ fill it in, tda_pqueue_deinit releases it, `= {}` is inert, and a copy of the
/// struct is a second owner of the same block.
typedef struct tda_PQueue {
    tda_Vec vec_; ///< the elems, kept a heap under cmp_
    tda_Cmp cmp_; ///< the order to serve in
    void *ctx_;   ///< passed to every call of cmp_
} tda_PQueue;

/// @name lifetime
/// @{

/// an empty queue that owns no block yet; allocates nothing, so it cannot fail
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param cmp the order to serve in, which only copy_assign, move_assign and swap replace
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al used for every later allocation
/// @return the new queue
/// @bigo{1}
[[nodiscard]] TDA_API
tda_PQueue tda_pqueue_init(tda_Layout elem, tda_Cmp cmp, void *ctx, tda_Al al);

/// an empty queue with room for 'cap' elems before the first growth
/// @param cap how many elems to make room for
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param cmp the order to serve in
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         cap * elem.size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_init_cap(size_t cap, tda_Layout elem, tda_Cmp cmp, void *ctx, tda_Al al, tda_PQueue *out);

/// a queue over a copy of 'len' elems read from 'data', in any order
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param cmp the order to serve in
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem.size overflows
/// @bigo{n}; heapifies in one pass, where 'len' pushes would cost O(n log n)
[[nodiscard]] TDA_API
tda_Status tda_pqueue_from_data(
    const void *data, size_t len, tda_Layout elem,
    tda_Cmp cmp, void *ctx,
    tda_Al al,
    tda_PQueue *out
);

/// a queue over a copy of the elems 's' views
/// @param s the view to copy, in any order; the elem layout is the span's
/// @param cmp the order to serve in
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al the allocator
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_from_span(tda_Span s, tda_Cmp cmp, void *ctx, tda_Al al, tda_PQueue *out);

/// releases what the queue holds and leaves it inert, `= {}`
/// @param self a queue, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_pqueue_deinit(tda_PQueue *self);

/// hands over the vec that holds the elems, leaving the queue inert
/// @param self the queue; inert afterwards
/// @return the vec, elems in heap order (not sorted), with the same capacity and
///         allocator; the comparator is dropped. tda_span_sort_heap over
///         tda_vec_to_span_mut sorts it in place. Nothing is copied, so this
///         cannot fail
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Vec tda_pqueue_into_vec(tda_PQueue *self);

/// @}

/// @name copy
/// @{

/// a new queue with the same elems and comparator, on the same allocator
/// @param self the queue to copy
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}; the heap is copied as is, not rebuilt
[[nodiscard]] TDA_API
tda_Status tda_pqueue_copy(const tda_PQueue *self, tda_PQueue *out);

/// a new queue with the same elems and comparator, on 'al'
/// @param self the queue to copy
/// @param al the allocator for the copy
/// @param[out] out the new queue, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}; the heap is copied as is, not rebuilt
[[nodiscard]] TDA_API
tda_Status tda_pqueue_copy_with(const tda_PQueue *self, tda_Al al, tda_PQueue *out);

/// overwrites the elems of 'self' with those of 'other', growing the block if needed
/// @param[in,out] self must have the same elem layout; takes the comparator of 'other' and
///                     keeps its allocator
/// @param other the queue to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'self' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_copy_assign(tda_PQueue *self, const tda_PQueue *other);

/// releases the elems of 'self' and hands it everything 'other' holds, its allocator
/// included, leaving 'other' empty on that allocator
/// @param[in,out] self must have the same elem layout, or be inert
/// @param[in,out] other emptied and still usable; may be 'self'
/// @bigo{1}
TDA_API
void tda_pqueue_move_assign(tda_PQueue *self, tda_PQueue *other);

/// @}

/// @name info
/// @{

/// how many elems are waiting
/// @param self the queue
/// @return the length
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_pqueue_len(const tda_PQueue *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_len(&self->vec_);
}

/// how many elems fit before the block must grow
/// @param self the queue
/// @return the capacity
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_pqueue_cap(const tda_PQueue *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_cap(&self->vec_);
}

/// the layout of one elem, as the pqueue was built with
/// @param self the pqueue
/// @return the size and the alignment; every elem is aligned to it
/// @bigo{1}
[[nodiscard]]
static inline tda_Layout tda_pqueue_elem_layout(const tda_PQueue *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_elem_layout(&self->vec_);
}

/// the allocator the queue was built with
/// @param self the queue
/// @return the view it was built with
/// @bigo{1}
[[nodiscard]]
static inline tda_Al tda_pqueue_al(const tda_PQueue *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_al(&self->vec_);
}

/// the order the queue serves in
/// @param self the queue
/// @return the comparator; only copy_assign, move_assign and swap change it
/// @bigo{1}
[[nodiscard]]
static inline tda_Cmp tda_pqueue_cmp(const tda_PQueue *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return self->cmp_;
}

/// the ctx the comparator is called with
/// @param self the queue
/// @return the ctx given at construction, borrowed; it travels with the comparator
/// @bigo{1}
[[nodiscard]]
static inline void *tda_pqueue_cmp_ctx(const tda_PQueue *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return self->ctx_;
}

/// @}

/// @name access
/// @{

/// the greatest elem by 'cmp', the one the next pop drops
/// @param self asserts the queue is not empty
/// @return valid until the queue next changes; read-only so writes cannot break the heap
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_pqueue_top(const tda_PQueue *self) {
    assert(self);
    assert(self->vec_.al_.vt);
    TDA_EXPECT(tda_vec_len(&self->vec_) > 0);

    return tda_vec_front(&self->vec_);
}

/// @}

/// @name mods
/// @{

/// puts a copy of 'val' in, sifting it up to its place
/// @param self the queue
/// @param val the elem to copy in; may be one of this queue's
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{log n}, plus the amortized cost of growing
[[nodiscard]] TDA_API
tda_Status tda_pqueue_push(tda_PQueue *self, const void *val);

/// puts a copy of every elem of 'src' in
/// @param self the queue
/// @param src must have the same elem layout and must not view this queue: growing may
///            free what it reads
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow; the queue is left unchanged
/// @bigo{n} into an empty queue, which heapifies once; k log n for k elems into n,
///         sifted up one by one. Grows at most once
[[nodiscard]] TDA_API
tda_Status tda_pqueue_extend(tda_PQueue *self, tda_Span src);

/// drops the greatest elem: the last one takes its place and is sifted down
/// @param self asserts the queue is not empty; tda_pqueue_pop_to keeps a copy
/// @bigo{log n}
TDA_API
void tda_pqueue_pop(tda_PQueue *self);

/// copies the greatest elem to 'out', then drops it
/// @param self asserts the queue is not empty
/// @param[out] out where the elem goes
/// @bigo{log n}
TDA_API
void tda_pqueue_pop_to(tda_PQueue *self, void *out);

/// puts a copy of 'val' in place of the greatest elem and sifts it down: a pop and a
/// push in one sift, allocating nothing. Read tda_pqueue_top first to keep the old top
/// @param self asserts the queue is not empty
/// @param val the elem to put in; may be tda_pqueue_top itself
/// @bigo{log n}
TDA_API
void tda_pqueue_replace_top(tda_PQueue *self, const void *val);

/// drops every elem, keeping the block
/// @param self the queue
/// @bigo{1}
TDA_API
void tda_pqueue_clear(tda_PQueue *self);

/// makes room for 'new_cap' elems
/// @param self the queue
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, or new_cap * elem.size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_reserve(tda_PQueue *self, size_t new_cap);

/// gives back the room above the length
/// @param self a length of 0 releases the block
/// @retval TDA_STATUS_OK on success, including when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the smaller block is refused; nothing changes
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_pqueue_shrink_to_fit(tda_PQueue *self);

/// exchanges the contents of two queues, comparators included
/// @param[in,out] self one queue
/// @param[in,out] other must have the same elem layout; may be 'self'. Each
///                      side keeps the allocator its elems came from, so the two may differ
/// @bigo{1}
TDA_API
void tda_pqueue_swap(tda_PQueue *self, tda_PQueue *other);

/// @}

/// @name to span
/// @{

/// a read-only view of the elems in heap order; only the first is in sorted place
/// @param self the queue
/// @return valid until the queue next changes; read-only to keep the heap intact
/// @bigo{1}
[[nodiscard]]
static inline tda_Span tda_pqueue_to_span(const tda_PQueue *self) {
    assert(self);
    assert(self->vec_.al_.vt);

    return tda_vec_to_span(&self->vec_);
}

/// @}

/// @name print
/// @{

/// writes the elems in heap order as [a, b, c], with no newline
/// @param self the queue
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_pqueue_fprint(const tda_PQueue *self, FILE *stream, tda_FPrint fprint, void *ctx);

/// tda_pqueue_fprint to stdout, and a newline
/// @param self the queue
/// @param fprint the printer, called once per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_pqueue_print(const tda_PQueue *self, tda_FPrint fprint, void *ctx);

/// @}

/// @name macros
/// @{

/// @cond
// returns 'self' after checking TDA_LAYOUT_OF(T), so a macro evaluates its pointer
// once; _Generic keeps the pointer's constness
[[nodiscard]]
static inline const tda_PQueue *tda_pqueue_typed_(const tda_PQueue *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_pqueue_elem_layout(self), elem));
    return self;
}

[[nodiscard]]
static inline tda_PQueue *tda_pqueue_typed_mut_(tda_PQueue *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_pqueue_elem_layout(self), elem));
    return self;
}

#define TDA_PQUEUE_TYPED_(T, self) \
    _Generic((self), const tda_PQueue *: tda_pqueue_typed_, tda_PQueue *: tda_pqueue_typed_mut_)((self), TDA_LAYOUT_OF(T))
/// @endcond

/// tda_pqueue_init with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param cmp the order to serve in
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al the allocator
/// @bigo{1}
#define TDA_PQUEUE_INIT(T, cmp, ctx, al) \
    tda_pqueue_init(TDA_LAYOUT_OF(T), (cmp), (ctx), (al))

/// tda_pqueue_init_cap with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param cap how many elems to make room for
/// @param cmp the order to serve in
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{n}
#define TDA_PQUEUE_INIT_CAP(T, cap, cmp, ctx, al, out) \
    tda_pqueue_init_cap((cap), TDA_LAYOUT_OF(T), (cmp), (ctx), (al), (out))

/// tda_pqueue_from_data with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param data the elems, typechecked as const T *
/// @param len how many to read
/// @param cmp the order to serve in
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @bigo{n}
#define TDA_PQUEUE_FROM_DATA(T, data, len, cmp, ctx, al, out) \
    tda_pqueue_from_data((const typeof(T) *){ (data) }, (len), TDA_LAYOUT_OF(T), (cmp), (ctx), (al), (out))

/// a new queue from the elems written out:
/// TDA_PQUEUE_OF(int32_t, tda_cmp_i32, nullptr, al, &q, 5, 3, 1)
/// @param T the elem type
/// @param cmp the order to serve in
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al the allocator
/// @param[out] out where the new queue is written
/// @param ... the elems, in any order, as a T initializer list, not empty
/// @bigo{n}
#define TDA_PQUEUE_OF(T, cmp, ctx, al, out, ...)                \
    tda_pqueue_from_data(                                       \
        (const typeof(T)[]){ __VA_ARGS__ },                     \
        sizeof((const typeof(T)[]){ __VA_ARGS__ }) / sizeof(T), \
        TDA_LAYOUT_OF(T), (cmp), (ctx), (al), (out))

/// tda_pqueue_top as a const T *
/// @param T the elem type
/// @param self the queue
/// @bigo{1}
#define TDA_PQUEUE_TOP_AS(T, self) \
    ((const typeof(T) *) tda_pqueue_top(TDA_PQUEUE_TYPED_(T, self)))

/// tda_pqueue_push from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param self the queue
/// @param val the value to copy in
/// @bigo{log n}
#define TDA_PQUEUE_PUSH(T, self, val) \
    tda_pqueue_push(TDA_PQUEUE_TYPED_(T, self), &(T[1]){ (val) }[0])

/// tda_pqueue_extend from the elems written out
/// @param T the elem type
/// @param self the queue
/// @param ... the elems, as a T initializer list, not empty
/// @bigo{n} into an empty queue, k log n otherwise
#define TDA_PQUEUE_EXTEND(T, self, ...) \
    tda_pqueue_extend((self), TDA_SPAN_OF(T, __VA_ARGS__))

/// tda_pqueue_pop_to with 'out' typechecked as T *
/// @param T the elem type
/// @param self the queue
/// @param[out] out where the elem goes
/// @bigo{log n}
#define TDA_PQUEUE_POP_TO(T, self, out) \
    tda_pqueue_pop_to(TDA_PQUEUE_TYPED_(T, self), (T *){ (out) })

/// tda_pqueue_replace_top from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param self the queue
/// @param val the value to put in
/// @bigo{log n}
#define TDA_PQUEUE_REPLACE_TOP(T, self, val) \
    tda_pqueue_replace_top(TDA_PQUEUE_TYPED_(T, self), &(T[1]){ (val) }[0])

/// @}

/// @}
