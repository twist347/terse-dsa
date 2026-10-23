#pragma once

#include "tda/algo/fn.h"
#include "tda/alloc/alloc.h"
#include "tda/core/check.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/status.h"

#include <stddef.h>

/// @file

/// @defgroup ds_list ds/list
/// @ingroup ds
/// @brief tda_List — an owning list whose positions never move
///
/// Doubly linked, one node per elem.
///
/// A node never moves, so a position stays valid as long as its elem does. The price is
/// an allocation per elem, scattered memory and no index: reaching the n-th elem is a
/// walk. Insertion and removal are O(1) at a node already in hand, where a vec pays O(n).
/// Debug builds assert that such a node belongs to the list, which walks it, so there
/// those ops cost O(n).
///
/// The relink ops (reverse, sort, merge, splice_node) move nodes, not elems, so a
/// borrowed node still names the same elem. That is why a list sorts itself instead of
/// using algo/sort.
///
/// The elems are not contiguous, so algo is reached by copying: tda_list_copy_to_span and
/// tda_list_copy_from_span. A node cannot change owner without moving, so the ops that
/// take nodes from another list (splice, splice_node, merge) relink only on one
/// allocator. Across two they copy the elems into new nodes and free the old ones, and
/// positions borrowed from 'src' die. swap takes one allocator only.
///
/// @par Example
/// @snippet ds/example_list.c build
/// @snippet ds/example_list.c walk
/// @snippet ds/example_list.c positions
/// @snippet ds/example_list.c relink
/// @{

/// Owning list whose positions never move.
/// Opaque: made by a constructor, released by tda_list_drop
typedef struct tda_List tda_List;

/// A position in the list.
/// Borrowed from the list; valid until its elem is removed, cleared, assigned over or
/// dropped with the list
typedef struct tda_ListNode tda_ListNode;

/// @name lifetime
/// @{

/// an empty list
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al used for every later allocation
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header cannot be allocated, or a node of this
///         elem size would not fit in size_t
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_list_new(tda_Layout elem, tda_Al *al, tda_List **out);

/// a list holding a copy of 'len' elems read from 'data', front to back
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or any node cannot be allocated;
///         nothing is leaked
/// @bigo{n}, one allocation per elem
[[nodiscard]] TDA_API
tda_Status tda_list_from_data(const void *data, size_t len, tda_Layout elem, tda_Al *al, tda_List **out);

/// a list holding a copy of the elems 's' views
/// @param s the view to copy; the elem layout is the span's
/// @param al the allocator
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_list_from_span(tda_Span s, tda_Al *al, tda_List **out);

/// releases every node and the list through the allocator it was built with
/// @param self null is a no-op
/// @bigo{n}
TDA_API
void tda_list_drop(tda_List *self);

/// @}

/// @name copy
/// @{

/// a new list with the same elems in the same order, on the same allocator
/// @param self the list to copy
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_list_copy(const tda_List *self, tda_List **out);

/// a new list with the same elems in the same order, on 'al'
/// @param self the list to copy
/// @param al the allocator for the copy
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_list_copy_with(const tda_List *self, tda_Al *al, tda_List **out);

/// overwrites the elems of 'self' with those of 'other', reusing its nodes and allocating
/// or releasing the difference
/// @param[in,out] self must have the same elem layout; keeps its allocator
/// @param other the list to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node cannot be allocated; 'self' is unchanged
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_list_copy_assign(tda_List *self, const tda_List *other);

/// moves the elems of 'other' into 'self', leaving 'other' empty
/// @param[in,out] self must have the same elem layout; its old elems are released, its
///                     allocator kept
/// @param[in,out] other emptied on success and still usable; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the allocators differ and a node cannot be
///         allocated; both are left unchanged
/// @bigo{k} on one allocator, for the k old nodes of 'self' freed one by one; k + n
///         across two, where the elems are copied and borrowed positions do not survive
[[nodiscard]] TDA_API
tda_Status tda_list_move_assign(tda_List *self, tda_List *other);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems front to back, byte for byte
/// @param a one list
/// @param b must have the same elem layout (asserted); a different length gives false
/// @return whether the lengths match and the bytes do
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_list_eq(const tda_List *a, const tda_List *b);

/// whether the two hold equal elems under 'eq'
/// @param a one list
/// @param b must have the same elem layout
/// @param eq asked of each pair until one fails
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_list_eq_by(const tda_List *a, const tda_List *b, tda_Eq eq, void *ctx);

/// @}

/// @name info
/// @{

/// how many elems the list holds
/// @param self the list
/// @return the length, stored, not counted
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_list_len(const tda_List *self);

/// the size of one elem
/// @param self the list
/// @return elem.size, the size of the elem layout
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_list_elem_size(const tda_List *self);

/// the layout of one elem, as the list was built with
/// @param self the list
/// @return the size and the alignment; every elem is aligned to it
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Layout tda_list_elem_layout(const tda_List *self);

/// the allocator the list was built with
/// @param self the list
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_list_al(const tda_List *self);

/// the smallest block that holds both the list struct and one node, for an allocator
/// whose blocks are all one layout, such as alloc/pool
/// @param elem the layout of one elem, asserted to be a type's and one tda_list_new
///             accepts
/// @return the larger size and the larger alignment of the two; under it tda_list_new or
///         the first push fails
/// @note a node of an elem smaller than about four pointers is smaller than the struct,
///       so every node of such a list leaves part of its block unused; alloc/slab sizes
///       the two apart
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Layout tda_list_block_layout(tda_Layout elem);

/// @}

/// @name access
/// @{

/// the front elem
/// @param self asserts the list is not empty
/// @return valid until that elem is removed
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_list_front(const tda_List *self);

/// the front elem, to write through
/// @copydetails tda_list_front
[[nodiscard]] TDA_API
void *tda_list_front_mut(tda_List *self);

/// the back elem
/// @copydetails tda_list_front
[[nodiscard]] TDA_API
const void *tda_list_back(const tda_List *self);

/// the back elem, to write through
/// @copydetails tda_list_front
[[nodiscard]] TDA_API
void *tda_list_back_mut(tda_List *self);

/// @}

/// @name nodes
/// @{

/// the position of the front elem
/// @param self the list
/// @return the node, or null while the list is empty
/// @bigo{1}
[[nodiscard]] TDA_API
const tda_ListNode *tda_list_front_node(const tda_List *self);

/// the position of the front elem, to write through
/// @copydetails tda_list_front_node
[[nodiscard]] TDA_API
tda_ListNode *tda_list_front_node_mut(tda_List *self);

/// the position of the back elem
/// @copydetails tda_list_front_node
[[nodiscard]] TDA_API
const tda_ListNode *tda_list_back_node(const tda_List *self);

/// the position of the back elem, to write through
/// @copydetails tda_list_front_node
[[nodiscard]] TDA_API
tda_ListNode *tda_list_back_node_mut(tda_List *self);

/// the first node from the front whose elem is equal to 'key'
/// @param self the list
/// @param key the address of the value looked for
/// @param eq the equality; passed per call, since a list is never told how its elems
///           compare
/// @param ctx passed to every call of 'eq'; may be null
/// @return the node, or null when no elem is equal
/// @bigo{n}
[[nodiscard]] TDA_API
const tda_ListNode *tda_list_find(const tda_List *self, const void *key, tda_Eq eq, void *ctx);

/// the same position, to write, insert or remove through
/// @copydetails tda_list_find
[[nodiscard]] TDA_API
tda_ListNode *tda_list_find_mut(tda_List *self, const void *key, tda_Eq eq, void *ctx);

/// the position after 'node'
/// @param node the position to step from
/// @return the next node, or null at the back
/// @bigo{1}
[[nodiscard]] TDA_API
const tda_ListNode *tda_list_node_next(const tda_ListNode *node);

/// the position after 'node', to write through
/// @copydetails tda_list_node_next
[[nodiscard]] TDA_API
tda_ListNode *tda_list_node_next_mut(tda_ListNode *node);

/// the position before 'node'
/// @param node the position to step from
/// @return the previous node, or null at the front
/// @bigo{1}
[[nodiscard]] TDA_API
const tda_ListNode *tda_list_node_prev(const tda_ListNode *node);

/// the position before 'node', to write through
/// @copydetails tda_list_node_prev
[[nodiscard]] TDA_API
tda_ListNode *tda_list_node_prev_mut(tda_ListNode *node);

/// the elem at 'node'
/// @param self the list 'node' is in, which knows where in a node its elem lies
/// @param node the position
/// @return valid until that elem is removed
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_list_node_elem(const tda_List *self, const tda_ListNode *node);

/// the elem at 'node', to write through
/// @copydetails tda_list_node_elem
[[nodiscard]] TDA_API
void *tda_list_node_elem_mut(tda_List *self, tda_ListNode *node);

/// @}

/// @name mods
/// @{

/// puts a copy of 'val' at the front, in a node of its own
/// @param self the list
/// @param val the elem to copy in
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the node cannot be allocated
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_list_push_front(tda_List *self, const void *val);

/// puts a copy of 'val' at the back, in a node of its own
/// @copydetails tda_list_push_front
[[nodiscard]] TDA_API
tda_Status tda_list_push_back(tda_List *self, const void *val);

/// drops the front elem and releases its node
/// @param self asserts the list is not empty
/// @bigo{1}
TDA_API
void tda_list_pop_front(tda_List *self);

/// drops the back elem and releases its node
/// @copydetails tda_list_pop_front
TDA_API
void tda_list_pop_back(tda_List *self);

/// puts a copy of 'val' in a new node before 'at'
/// @param self the list
/// @param at a position, asserted to be one of this list's own
/// @param val the elem to copy in
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the node cannot be allocated
/// @bigo{1}; finding 'at' is the caller's walk
[[nodiscard]] TDA_API
tda_Status tda_list_insert_before(tda_List *self, tda_ListNode *at, const void *val);

/// puts a copy of 'val' in a new node after 'at'
/// @copydetails tda_list_insert_before
[[nodiscard]] TDA_API
tda_Status tda_list_insert_after(tda_List *self, tda_ListNode *at, const void *val);

/// takes 'node' out of the list and releases it
/// @param self the list
/// @param node a position, asserted to be one of this list's own; pointers into it die
///             here, other positions are untouched
/// @bigo{1}
TDA_API
void tda_list_remove(tda_List *self, tda_ListNode *node);

/// drops every elem and releases every node
/// @param self the list
/// @bigo{n}
TDA_API
void tda_list_clear(tda_List *self);

/// keeps the elems 'pred' passes and releases the nodes of the rest
/// @param self the list; kept nodes do not move, so pointers into them stay valid
/// @param pred asked once per elem, front to back; must not change the list
/// @param ctx handed to 'pred'
/// @bigo{n}
TDA_API
void tda_list_retain(tda_List *self, tda_Pred pred, void *ctx);

/// moves every node of 'src' to the front of 'self', leaving 'src' empty
/// @param self the list written into
/// @param[in,out] src must have the same elem layout and not be 'self' (asserted);
///                    emptied on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the allocators differ and a node cannot be
///         allocated; both are left unchanged
/// @bigo{1} on one allocator, n across two, where the elems are copied and borrowed
///         positions do not survive
[[nodiscard]] TDA_API
tda_Status tda_list_splice_front(tda_List *self, tda_List *src);

/// moves every node of 'src' to the back of 'self', leaving 'src' empty
/// @copydetails tda_list_splice_front
[[nodiscard]] TDA_API
tda_Status tda_list_splice_back(tda_List *self, tda_List *src);

/// exchanges the contents of two lists
/// @param self one list
/// @param other must have the same elem layout and allocator; the nodes do not move, so
///              borrowed positions stay valid
/// @note a node belongs to the allocator that made it. Across allocators use
///       tda_list_copy_with, then tda_list_move_assign
/// @bigo{1}
TDA_API
void tda_list_swap(tda_List *self, tda_List *other);

/// moves one node from 'src' into 'self' before 'at'
/// @param self the list written into
/// @param at asserted to be one of 'self''s own; null means the back
/// @param[in,out] src may be 'self', moving the node within one list
/// @param node asserted to be one of 'src''s own and not 'at'
/// @retval TDA_STATUS_OK on success; on one allocator the node keeps its address, so
///         borrowed pointers to it stay valid
/// @retval TDA_STATUS_ERR_NO_MEM when the allocators differ and the new node cannot be
///         allocated; across two the elem is copied and the address does not survive
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_list_splice_node(tda_List *self, tda_ListNode *at, tda_List *src, tda_ListNode *node);

/// @}

/// @name relink
/// @{

/// reverses the order by relinking
/// @param self the list; every node keeps its address and its elem
/// @bigo{n}, no allocation
TDA_API
void tda_list_reverse(tda_List *self);

/// sorts by relinking; stable, no allocation
/// @param self the list
/// @param cmp the order to sort under
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{n log n}; elems stay in their nodes, so a borrowed position still names the
///         same elem, which sorting a copy could not give
TDA_API
void tda_list_sort(tda_List *self, tda_Cmp cmp, void *ctx);

/// merges the sorted 'src' into the sorted 'self', leaving 'src' empty
/// @param self must already be sorted under 'cmp'
/// @param[in,out] src must have the same elem layout, be sorted under 'cmp' and not be
///                    'self' (asserted); on ties elems of 'self' come first
/// @param cmp the order both are already in
/// @param ctx passed to every call of 'cmp'; may be null
/// @retval TDA_STATUS_OK on success; on one allocator the nodes of 'src' are relinked
///         and borrowed positions survive
/// @retval TDA_STATUS_ERR_NO_MEM when the allocators differ and a node cannot be
///         allocated; both are left unchanged. Across two the elems of 'src' are copied
///         and its borrowed positions do not survive
/// @bigo{n + m}; splice_back then sort would cost O(n log n)
[[nodiscard]] TDA_API
tda_Status tda_list_merge(tda_List *self, tda_List *src, tda_Cmp cmp, void *ctx);

/// @}

/// @name copy to span
/// @{

/// writes every elem into 'dst', front to back; the list's only way to algo, as there
/// is no contiguous block to view
/// @param self the list
/// @param dst must have the same elem layout and be exactly as long as the list
/// @bigo{n}
TDA_API
void tda_list_copy_to_span(const tda_List *self, tda_SpanMut dst);

/// overwrites every elem from 'src', front to back; pairs with tda_list_copy_to_span to
/// put an algo result back
/// @param self the list
/// @param src must have the same elem layout and be exactly as long as the list; the nodes
///            stay in place, so this cannot fail
/// @bigo{n}
TDA_API
void tda_list_copy_from_span(tda_List *self, tda_Span src);

/// @}

/// @name print
/// @{

/// writes the elems front to back as [a, b, c] and a newline
/// @param self the list
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_list_fprint(const tda_List *self, FILE *stream, tda_FPrint fprint);

/// tda_list_fprint to stdout
/// @param self the list
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_list_print(const tda_List *self, tda_FPrint fprint);

/// @}

/// @name macros
/// @{

/// @cond
// returns 'self' after checking TDA_LAYOUT_OF(T), so a macro evaluates its handle
// once; _Generic keeps the handle's constness
[[nodiscard]]
static inline const tda_List *tda_list_typed_c_(const tda_List *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_list_elem_layout(self), elem));
    return self;
}

[[nodiscard]]
static inline tda_List *tda_list_typed_(tda_List *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_list_elem_layout(self), elem));
    return self;
}

#define TDA_LIST_TYPED_(T, self) \
    _Generic((self), const tda_List *: tda_list_typed_c_, tda_List *: tda_list_typed_)((self), TDA_LAYOUT_OF(T))
/// @endcond

/// tda_list_new with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new list is written
/// @bigo{1}
#define TDA_LIST_NEW(T, al, out) \
    tda_list_new(TDA_LAYOUT_OF(T), (al), (out))

/// tda_list_from_data with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param data the elems, typechecked as const T *
/// @param len how many to read
/// @param al the allocator
/// @param[out] out where the new list is written
/// @bigo{n}
#define TDA_LIST_FROM_DATA(T, data, len, al, out) \
    tda_list_from_data((const T *){ (data) }, (len), TDA_LAYOUT_OF(T), (al), (out))

/// a new list from the elems written out: TDA_LIST_OF(int32_t, al, &l, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new list is written
/// @param ... the elems, front to back, as a T initializer list, not empty
/// @bigo{n}
#define TDA_LIST_OF(T, al, out, ...)                    \
    tda_list_from_data(                                 \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        TDA_LAYOUT_OF(T), (al), (out))

/// tda_list_front as a const T *
/// @param T the elem type
/// @param self the list
/// @bigo{1}
#define TDA_LIST_FRONT_AS(T, self) \
    ((const T *) tda_list_front(TDA_LIST_TYPED_(T, self)))

/// tda_list_front_mut as a T *
/// @copydetails TDA_LIST_FRONT_AS
#define TDA_LIST_FRONT_MUT_AS(T, self) \
    ((T *) tda_list_front_mut(TDA_LIST_TYPED_(T, self)))

/// tda_list_back as a const T *
/// @copydetails TDA_LIST_FRONT_AS
#define TDA_LIST_BACK_AS(T, self) \
    ((const T *) tda_list_back(TDA_LIST_TYPED_(T, self)))

/// tda_list_back_mut as a T *
/// @copydetails TDA_LIST_FRONT_AS
#define TDA_LIST_BACK_MUT_AS(T, self) \
    ((T *) tda_list_back_mut(TDA_LIST_TYPED_(T, self)))

/// walks the list front to back, binding 'node' to each position in turn
/// @param node the name of the loop variable, a const tda_ListNode *
/// @param self the list
/// @warning the loop steps through the current node after the body, so removing it in
///          the body reads freed memory. To remove while walking, take the next first:
/// @code
/// for (tda_ListNode *node = tda_list_front_node_mut(l), *next; node; node = next) {
///     next = tda_list_node_next_mut(node);
///     if (drop_it(node)) {
///         tda_list_remove(l, node);
///     }
/// }
/// @endcode
#define TDA_LIST_FOR_EACH(node, self)                          \
    for (const tda_ListNode *node = tda_list_front_node(self); \
         node;                                                 \
         node = tda_list_node_next(node))

/// the same walk over positions that may be written through
/// @copydetails TDA_LIST_FOR_EACH
#define TDA_LIST_FOR_EACH_MUT(node, self)                         \
    for (tda_ListNode *node = tda_list_front_node_mut(self);      \
         node;                                                    \
         node = tda_list_node_next_mut(node))

/// tda_list_find from a value rather than an address
/// @param T the elem type; scalars only, as 'val' becomes a compound literal
/// @param self the list
/// @param val the value looked for
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @bigo{n}
#define TDA_LIST_FIND(T, self, val, eq, ctx) \
    tda_list_find(TDA_LIST_TYPED_(T, self), &(T){ (val) }, (eq), (ctx))

/// tda_list_find_mut from a value rather than an address
/// @copydetails TDA_LIST_FIND
#define TDA_LIST_FIND_MUT(T, self, val, eq, ctx) \
    tda_list_find_mut(TDA_LIST_TYPED_(T, self), &(T){ (val) }, (eq), (ctx))

/// tda_list_node_elem as a const T *
/// @param T the elem type
/// @param self the list 'node' is in
/// @param node the position
/// @bigo{1}
#define TDA_LIST_NODE_ELEM_AS(T, self, node) \
    ((const T *) tda_list_node_elem(TDA_LIST_TYPED_(T, self), (node)))

/// tda_list_node_elem_mut as a T *
/// @copydetails TDA_LIST_NODE_ELEM_AS
#define TDA_LIST_NODE_ELEM_MUT_AS(T, self, node) \
    ((T *) tda_list_node_elem_mut(TDA_LIST_TYPED_(T, self), (node)))

/// tda_list_push_front from a value rather than an address
/// @param T the elem type; scalars only, as 'val' becomes a compound literal
/// @param self the list
/// @param val the value to copy in
/// @bigo{1}
#define TDA_LIST_PUSH_FRONT(T, self, val) \
    tda_list_push_front(TDA_LIST_TYPED_(T, self), &(T){ (val) })

/// tda_list_push_back from a value rather than an address
/// @copydetails TDA_LIST_PUSH_FRONT
#define TDA_LIST_PUSH_BACK(T, self, val) \
    tda_list_push_back(TDA_LIST_TYPED_(T, self), &(T){ (val) })

/// tda_list_insert_before from a value rather than an address
/// @param T the elem type; scalars only, as 'val' becomes a compound literal
/// @param self the list
/// @param at the position to insert at
/// @param val the value to copy in
/// @bigo{1}
#define TDA_LIST_INSERT_BEFORE(T, self, at, val) \
    tda_list_insert_before(TDA_LIST_TYPED_(T, self), (at), &(T){ (val) })

/// tda_list_insert_after from a value rather than an address
/// @copydetails TDA_LIST_INSERT_BEFORE
#define TDA_LIST_INSERT_AFTER(T, self, at, val) \
    tda_list_insert_after(TDA_LIST_TYPED_(T, self), (at), &(T){ (val) })

/// @}

/// @}
