#pragma once

#include "tda/alloc/alloc.h"
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
/// Doubly linked: every elem sits in a node of its own, held together by pointers rather
/// than by being neighbours in memory.
///
/// A node never moves, so a position stays good for as long as the elem does. The cost is
/// everything contiguity gives — an allocation per elem, memory scattered across the heap,
/// no index, and reaching the n-th elem is a walk. Insertion and removal are O(1) wherever
/// a node is already in hand, where a vec pays O(n) to open or close a gap.
///
/// The relink family — reverse, sort, merge and splice_node — moves NODES rather than
/// elems, so a borrowed node still names the elem it named before. That is why a list
/// sorts itself instead of handing the work to algo/sort.
///
/// The bridge to algo is tda_list_copy_to_span / tda_list_copy_from_span: the elems are
/// not contiguous, so there is nothing to view and nothing cheaper than a copy. Splice and
/// swap want both lists on one allocator — a node cannot change owner without moving.
///
/// @par Example
/// @snippet ds/example_list.c build
/// @snippet ds/example_list.c walk
/// @snippet ds/example_list.c positions
/// @snippet ds/example_list.c relink
/// @{

/// Owning list whose positions never move.
/// An opaque handle: it comes from a constructor and goes back to tda_list_drop
typedef struct tda_List tda_List;

/// A position in the list.
/// Borrowed from the list and invalidated only by removing that very elem
typedef struct tda_ListNode tda_ListNode;

/// @name lifetime
/// @{

/// an empty list
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator, kept for everything after
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header cannot be allocated
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_list_new(size_t elem_size, tda_Al *al, tda_List **out);

/// a list holding a copy of 'len' elems read from 'data', front to back
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or any node cannot be allocated;
///         nothing is left behind on the way out
/// @bigo{n} — one allocation per elem
[[nodiscard]] TDA_API
tda_Status tda_list_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_List **out);

/// a list holding a copy of what 's' views, taking its elem_size
/// @param s the view to copy
/// @param al the allocator
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_list_from_span(tda_Span s, tda_Al *al, tda_List **out);

/// releases every node and the list through the allocator it was built with
/// @param self null is a no-op, so this is safe on a partly built object
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
/// @param al where the copy lives; tda_list_copy is this one with the allocator of 'self'
/// @param[out] out the new list, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or any node cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_list_copy_with(const tda_List *self, tda_Al *al, tda_List **out);

/// overwrites the elems of 'self' with those of 'other', reusing the nodes it already has
/// and taking or releasing the difference
/// @param[in,out] self must have the same elem_size; keeps its own allocator, and
///                     'self' == 'other' is a no-op
/// @param other the list to copy from
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when a node cannot be allocated, leaving 'self' as
///         it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_list_copy_assign(tda_List *self, const tda_List *other);

/// moves the elems of 'other' into 'self', leaving 'other' empty
/// @param[in,out] self must have the same elem_size; releases what it held and keeps
///                     its own allocator. 'self' == 'other' is a no-op
/// @param[in,out] other the list to move from; emptied on success and still usable, on
///                      its own allocator
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and a node cannot be allocated,
///         leaving both as they were
/// @bigo{1} on one allocator, n on two — a node belongs to the allocator that made it,
///          so across two the elems are copied and the borrowed positions do not survive
[[nodiscard]] TDA_API
tda_Status tda_list_move_assign(tda_List *self, tda_List *other);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems front to back, byte for byte
/// @param a one list
/// @param b must have the same elem_size — a mismatch there is a programmer error, not a
///          false; a differing length is just false
/// @return whether the lengths match and the bytes do
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_list_eq(const tda_List *a, const tda_List *b);

/// whether the two hold equal elems under 'eq'
/// @param a one list
/// @param b must have the same elem_size as 'a'
/// @param eq asked of every pair until one says no
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_list_eq_by(const tda_List *a, const tda_List *b, tda_Eq eq);

/// @}

/// @name info
/// @{

/// how many elems the list holds
/// @param self the list
/// @return the length, kept as a number rather than walked for
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_list_len(const tda_List *self);

/// the size of one elem, as named at construction
/// @param self the list
/// @return elem_size
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_list_elem_size(const tda_List *self);

/// the allocator the list was built with
/// @param self the list
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_list_al(const tda_List *self);

/// @}

/// @name access
/// @{

/// the front elem
/// @param self asserts the list is not empty
/// @return a pointer into its node, good until that elem is removed
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
/// @param eq the equality, asked here rather than fixed at construction: a list keeps no
///           order of its own and so was never told how its elems compare
/// @return the node, or null when no elem is equal. Hold it to read the elem, or to
///         insert or remove at that position
/// @bigo{n}
[[nodiscard]] TDA_API
const tda_ListNode *tda_list_find(const tda_List *self, const void *key, tda_Eq eq);

/// the same position as one to write, insert or remove through
/// @copydetails tda_list_find
[[nodiscard]] TDA_API
tda_ListNode *tda_list_find_mut(tda_List *self, const void *key, tda_Eq eq);

/// the position after 'node'
/// @param node the position to step from
/// @return the next node, or null at the back — which is what ends a walk
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
/// @param node the position
/// @return a pointer into the node, good until that elem is removed
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_list_node_elem(const tda_ListNode *node);

/// the elem at 'node', to write through
/// @copydetails tda_list_node_elem
[[nodiscard]] TDA_API
void *tda_list_node_elem_mut(tda_ListNode *node);

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
/// @bigo{1} — the walk to 'at' is the caller's, and this is what it pays for
[[nodiscard]] TDA_API
tda_Status tda_list_insert_before(tda_List *self, tda_ListNode *at, const void *val);

/// puts a copy of 'val' in a new node after 'at'
/// @copydetails tda_list_insert_before
[[nodiscard]] TDA_API
tda_Status tda_list_insert_after(tda_List *self, tda_ListNode *at, const void *val);

/// takes 'node' out of the list and releases it
/// @param self the list
/// @param node a position, asserted to be one of this list's own; every pointer into it
///             dies here, and no other position is touched
/// @bigo{1}
TDA_API
void tda_list_remove(tda_List *self, tda_ListNode *node);

/// drops every elem and releases every node
/// @param self the list
/// @bigo{n}
TDA_API
void tda_list_clear(tda_List *self);

/// moves every node of 'src' to the front of 'self', leaving 'src' empty
/// @param self the list written into
/// @param[in,out] src must have the same elem_size; emptied on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and a node
///         cannot be allocated, leaving both as they were
/// @bigo{1} on one allocator, n on two — a node belongs to the allocator that made it, so
///          across two the elems are copied and the borrowed positions do not survive
[[nodiscard]] TDA_API
tda_Status tda_list_splice_front(tda_List *self, tda_List *src);

/// moves every node of 'src' to the back of 'self', leaving 'src' empty
/// @copydetails tda_list_splice_front
[[nodiscard]] TDA_API
tda_Status tda_list_splice_back(tda_List *self, tda_List *src);

/// exchanges the contents of the two
/// @param self one list
/// @param other must have the same elem_size and the same allocator: the nodes change
///              list without moving, so every borrowed position stays valid
/// @note two allocators are a broken precondition, not a runtime state: a node belongs to
///       the one that made it. Across two: tda_list_copy_with, then tda_list_move_assign
/// @bigo{1}
TDA_API
void tda_list_swap(tda_List *self, tda_List *other);

/// moves ONE node out of 'src' and into 'self' before 'at'
/// @param self the list written into
/// @param at where to put it; null means the back. Asserted to be one of 'self''s own
/// @param[in,out] src may be 'self', which moves the node within one list
/// @param node the node to move, asserted to be one of 'src''s own and not 'at'
/// @retval TDA_STATUS_OK on success; on one allocator the node keeps its address, so a
///         borrowed pointer to it survives the move
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and the new
///         node cannot be allocated; across two the elem is copied and the address does
///         not survive
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Status tda_list_splice_node(tda_List *self, tda_ListNode *at, tda_List *src, tda_ListNode *node);

/// @}

/// @name relink
/// @{

/// reverses the order by relinking
/// @param self the list; every node keeps its address and its elem, only the order changes
/// @bigo{n} — nothing is allocated
TDA_API
void tda_list_reverse(tda_List *self);

/// sorts by relinking: stable, and nothing is allocated
/// @param self the list
/// @param cmp the order to sort under
/// @bigo{n log n} — the nodes change places while the elems stay where they are, so a
///                  borrowed position still names the elem it named before. That is what
///                  sorting a copy could not give, and why this is not algo/sort's job
TDA_API
void tda_list_sort(tda_List *self, tda_Cmp cmp);

/// merges the sorted 'src' into the sorted 'self', leaving 'src' empty
/// @param self must already be sorted under 'cmp'
/// @param[in,out] src must have the same elem_size and be sorted under 'cmp'; equal elems
///                    of 'self' are kept before those of 'src'
/// @param cmp the order both are already in
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and a node
///         cannot be allocated, leaving both as they were
/// @bigo{n + m} — reaching the same result through splice_back plus sort would cost
///                O(n log n) and throw away what is already known
[[nodiscard]] TDA_API
tda_Status tda_list_merge(tda_List *self, tda_List *src, tda_Cmp cmp);

/// @}

/// @name copy to span
/// @{

/// writes every elem into 'dst', front to back. The list's only bridge to algo: the elems
/// are not contiguous, so there is nothing to view and nothing cheaper than a copy
/// @param self the list
/// @param dst must have the same elem_size and be exactly as long as the list
/// @bigo{n}
TDA_API
void tda_list_copy_to_span(const tda_List *self, tda_SpanMut dst);

/// overwrites every elem from 'src' — the pair to tda_list_copy_to_span: take the
/// contents out, hand them to algo, put the answer back
/// @param self the list
/// @param src must have the same elem_size and be exactly as long as the list. The nodes
///            stay where they are, so nothing is allocated and nothing can fail
/// @bigo{n}
TDA_API
void tda_list_copy_from_span(tda_List *self, tda_Span src);

/// @}

/// @name print
/// @{

/// writes the elems to a stream front to back as [a, b, c], followed by a newline
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

/// tda_list_new with sizeof(T) for the elem size
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new list is written
/// @bigo{1}
#define TDA_LIST_NEW(T, al, out) \
    tda_list_new(sizeof(T), (al), (out))

/// tda_list_from_data with sizeof(T)
/// @param T the elem type
/// @param data the elems to copy in, made to typecheck as a const T *
/// @param len how many elems to read from 'data'
/// @param al the allocator
/// @param[out] out where the new list is written
/// @bigo{n}
#define TDA_LIST_FROM_DATA(T, data, len, al, out) \
    tda_list_from_data((const T *){ (data) }, (len), sizeof(T), (al), (out))

/// a new list from the elems written out: TDA_LIST_OF(int32_t, al, &l, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new list is written
/// @param ... the elems, front to back, as a T initializer list
/// @bigo{n}
#define TDA_LIST_OF(T, al, out, ...)                    \
    tda_list_from_data(                                 \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        sizeof(T), (al), (out))

/// tda_list_front as a const T *
/// @param T the elem type
/// @param self the list
/// @bigo{1}
#define TDA_LIST_FRONT_AS(T, self) \
    ((const T *) tda_list_front((self)))

/// tda_list_front_mut as a T *
/// @copydetails TDA_LIST_FRONT_AS
#define TDA_LIST_FRONT_MUT_AS(T, self) \
    ((T *) tda_list_front_mut((self)))

/// tda_list_back as a const T *
/// @copydetails TDA_LIST_FRONT_AS
#define TDA_LIST_BACK_AS(T, self) \
    ((const T *) tda_list_back((self)))

/// tda_list_back_mut as a T *
/// @copydetails TDA_LIST_FRONT_AS
#define TDA_LIST_BACK_MUT_AS(T, self) \
    ((T *) tda_list_back_mut((self)))

/// walks the list front to back, binding 'node' to each position in turn
/// @param node the name the loop variable takes; it is a const tda_ListNode *
/// @param self the list
/// @note the step to the next position happens after the body, through the node the body
///       saw — so removing that node inside the loop cuts the walk. Take the next
///       position first, or walk by hand
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
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the list
/// @param val the value looked for
/// @param eq the equality
/// @bigo{n}
#define TDA_LIST_FIND(T, self, val, eq) \
    tda_list_find((self), &(T){ (val) }, (eq))

/// tda_list_find_mut from a value rather than an address
/// @copydetails TDA_LIST_FIND
#define TDA_LIST_FIND_MUT(T, self, val, eq) \
    tda_list_find_mut((self), &(T){ (val) }, (eq))

/// tda_list_node_elem as a const T *
/// @param T the elem type
/// @param node the position
/// @bigo{1}
#define TDA_LIST_NODE_ELEM_AS(T, node) \
    ((const T *) tda_list_node_elem((node)))

/// tda_list_node_elem_mut as a T *
/// @copydetails TDA_LIST_NODE_ELEM_AS
#define TDA_LIST_NODE_ELEM_MUT_AS(T, node) \
    ((T *) tda_list_node_elem_mut((node)))

/// tda_list_push_front from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the list
/// @param val the value to copy in
/// @bigo{1}
#define TDA_LIST_PUSH_FRONT(T, self, val) \
    tda_list_push_front((self), &(T){ (val) })

/// tda_list_push_back from a value rather than an address
/// @copydetails TDA_LIST_PUSH_FRONT
#define TDA_LIST_PUSH_BACK(T, self, val) \
    tda_list_push_back((self), &(T){ (val) })

/// tda_list_insert_before from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the list
/// @param at the position to insert at
/// @param val the value to copy in
/// @bigo{1}
#define TDA_LIST_INSERT_BEFORE(T, self, at, val) \
    tda_list_insert_before((self), (at), &(T){ (val) })

/// tda_list_insert_after from a value rather than an address
/// @copydetails TDA_LIST_INSERT_BEFORE
#define TDA_LIST_INSERT_AFTER(T, self, at, val) \
    tda_list_insert_after((self), (at), &(T){ (val) })

/// @}

/// @}
