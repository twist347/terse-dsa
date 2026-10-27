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
/// tda_deque_to_span either, and no linearize to provide one: making the ring contiguous
/// would be a mutation the next push_front undoes. Instead algo gets tda_deque_to_spans,
/// the two runs as they lie, for whatever works run by run, and the pair
/// tda_deque_copy_to_span / tda_deque_copy_from_span for whatever needs one run.
///
/// Pointers are not stable: growing moves every elem. For stable positions use ds/list.
///
/// Out-of-range indices assert. Only the ops that allocate return a tda_Status.
///
/// @par Example
/// @snippet ds/example_deque.c build
/// @snippet ds/example_deque.c ends
/// @snippet ds/example_deque.c index
/// @snippet ds/example_deque.c algo
/// @{

/// Owning queue with two cheap ends. A value the caller holds wherever it likes: an init
/// that cannot fail returns it, one that can and a from_ fill it in, tda_deque_deinit
/// releases its block. `= {}` is the inert state, which tda_deque_deinit accepts,
/// tda_deque_move_assign takes as its target, and every other op asserts.
///
/// The fields belong to the deque: read and change them only through the functions here.
///
/// @warning a copy of the struct is a second owner of the same block: releasing both
///          frees it twice, and growing one leaves the other dangling. Pass a pointer,
///          and hand a deque over with tda_deque_move_assign.
typedef struct tda_Deque {
    void *data_;      ///< the ring; null while cap is 0
    size_t len_;      ///< how many elems it holds
    size_t cap_;      ///< how many fit before the ring grows
    /// the slot holding the front elem. Kept beside len_ rather than a tail index: with
    /// two indices a full ring and an empty one look exactly alike
    size_t head_;
    tda_Layout elem_; ///< the layout of one elem
    tda_Al al_;       ///< where the ring comes from; moves along with the deque
} tda_Deque;

/// @cond
// the slot of the elem 'idx' places past the front, one lap back when that passes the
// last slot. Compared against the room left rather than through head + idx, which
// cannot overflow. The one place an index becomes a slot; deque.c goes through it too
static inline size_t tda_deque_slot_of_(const tda_Deque *self, size_t idx) {
    const size_t room = self->cap_ - self->head_;

    return idx < room ? self->head_ + idx : idx - room;
}

static inline const unsigned char *tda_deque_elem_at_(const tda_Deque *self, size_t idx) {
    return (const unsigned char *) self->data_ + tda_deque_slot_of_(self, idx) * self->elem_.size;
}

static inline unsigned char *tda_deque_elem_at_mut_(tda_Deque *self, size_t idx) {
    return (unsigned char *) self->data_ + tda_deque_slot_of_(self, idx) * self->elem_.size;
}
/// @endcond

/// @name lifetime
/// @{

/// an empty deque that owns no block yet; allocates nothing, so it cannot fail
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al used for every later allocation
/// @return the new deque
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Deque tda_deque_init(tda_Layout elem, tda_Al al);

/// a deque of 'len' zeroed elems, ready to be written through get_mut or set
/// @param len how many elems
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem.size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_init_len(size_t len, tda_Layout elem, tda_Al al, tda_Deque *out);

/// an empty deque with room for 'cap' elems before the first growth
/// @param cap how many elems to make room for
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         cap * elem.size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_init_cap(size_t cap, tda_Layout elem, tda_Al al, tda_Deque *out);

/// a deque holding a copy of 'len' elems read from 'data', front to back
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new deque, written only on success; its ring starts out unwrapped
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem.size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_from_data(const void *data, size_t len, tda_Layout elem, tda_Al al, tda_Deque *out);

/// a deque holding a copy of the elems 's' views
/// @param s the view to copy; the elem layout is the span's
/// @param al the allocator
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_from_span(tda_Span s, tda_Al al, tda_Deque *out);

/// releases what the deque holds and leaves it inert, `= {}`
/// @param self a deque, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_deque_deinit(tda_Deque *self);

/// @}

/// @name copy
/// @{

/// a new deque with the same elems in the same order, on the same allocator
/// @param self the deque to copy
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_copy(const tda_Deque *self, tda_Deque *out);

/// a new deque with the same elems in the same order, on 'al'
/// @param self the deque to copy
/// @param al the allocator for the copy
/// @param[out] out the new deque, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_copy_with(const tda_Deque *self, tda_Al al, tda_Deque *out);

/// overwrites the elems of 'self' with those of 'other', growing the block if needed
/// @param[in,out] self must have the same elem layout; keeps its allocator
/// @param other the deque to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'self' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_copy_assign(tda_Deque *self, const tda_Deque *other);

/// releases the elems of 'self' and hands it everything 'other' holds, its allocator
/// included, leaving 'other' empty on that allocator
/// @param[in,out] self must have the same elem layout, or be inert
/// @param[in,out] other emptied and still usable; may be 'self'
/// @bigo{1}
TDA_API
void tda_deque_move_assign(tda_Deque *self, tda_Deque *other);

/// writes every elem into 'dst' in ring order, in at most two memcpy since the contents
/// are at most two runs; how a deque reaches an algo that needs one run, like sort
/// @param self the deque
/// @param dst must have the same elem layout and be exactly as long as the deque
/// @bigo{n}
TDA_API
void tda_deque_copy_to_span(const tda_Deque *self, tda_SpanMut dst);

/// overwrites every elem from 'src', in ring order; pairs with tda_deque_copy_to_span to
/// put an algo result back
/// @param self the deque
/// @param src must have the same elem layout and be exactly as long as the deque; nothing
///            is allocated, so this cannot fail
/// @bigo{n}
TDA_API
void tda_deque_copy_from_span(tda_Deque *self, tda_Span src);

/// @}

/// @name to spans
/// @{

/// views the contents as the two runs they lie in: the front one from head to the end of
/// the block, then the back one from its start
/// @param self the deque; the views are valid until it next changes
/// @param[out] out_front the run holding the front elem; empty only on an empty deque
/// @param[out] out_back the rest, empty unless the ring wraps
/// @bigo{1}
TDA_API
void tda_deque_to_spans(const tda_Deque *self, tda_Span *out_front, tda_Span *out_back);

/// tda_deque_to_spans, writable
/// @copydetails tda_deque_to_spans
TDA_API
void tda_deque_to_spans_mut(tda_Deque *self, tda_SpanMut *out_front, tda_SpanMut *out_back);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems in ring order, byte for byte
/// @param a one deque
/// @param b must have the same elem layout (asserted); a different length gives false
/// @return whether the lengths match and the elems do, wherever either ring starts
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_deque_eq(const tda_Deque *a, const tda_Deque *b);

/// whether the two hold equal elems under 'eq'
/// @param a one deque
/// @param b must have the same elem layout
/// @param eq asked of each pair until one fails
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_deque_eq_by(const tda_Deque *a, const tda_Deque *b, tda_Eq eq, void *ctx);

/// @}

/// @name info
/// @{

/// how many elems the deque holds
/// @param self the deque
/// @return the length
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_deque_len(const tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);

    return self->len_;
}

/// how many elems fit before the block must grow
/// @param self the deque
/// @return the capacity; it says nothing about where the ring starts
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_deque_cap(const tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);

    return self->cap_;
}

/// the layout of one elem, as the deque was built with
/// @param self the deque
/// @return the size and the alignment; every elem is aligned to it
/// @bigo{1}
[[nodiscard]]
static inline tda_Layout tda_deque_elem_layout(const tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);

    return self->elem_;
}

/// how many bytes the elems take
/// @param self the deque
/// @return len * elem.size
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_deque_bytes(const tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);

    return self->len_ * self->elem_.size;
}

/// the allocator the deque was built with
/// @param self the deque
/// @return the view it was built with
/// @bigo{1}
[[nodiscard]]
static inline tda_Al tda_deque_al(const tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);

    return self->al_;
}

/// @}

/// @name access
/// @{

/// the front elem
/// @param self asserts the deque is not empty
/// @return valid until the next op that may reallocate
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_deque_front(const tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(self->len_ > 0);

    return tda_deque_elem_at_(self, 0);
}

/// the front elem, to write through
/// @copydetails tda_deque_front
[[nodiscard]]
static inline void *tda_deque_front_mut(tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(self->len_ > 0);

    return tda_deque_elem_at_mut_(self, 0);
}

/// the back elem
/// @copydetails tda_deque_front
[[nodiscard]]
static inline const void *tda_deque_back(const tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(self->len_ > 0);

    return tda_deque_elem_at_(self, self->len_ - 1);
}

/// the back elem, to write through
/// @copydetails tda_deque_front
[[nodiscard]]
static inline void *tda_deque_back_mut(tda_Deque *self) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(self->len_ > 0);

    return tda_deque_elem_at_mut_(self, self->len_ - 1);
}

/// the elem at 'idx'
/// @param self the deque
/// @param idx counts from the front, so 0 is the front elem wherever the ring starts;
///            asserts idx < len
/// @return valid until the next op that may reallocate
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_deque_get(const tda_Deque *self, size_t idx) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(idx < self->len_);

    return tda_deque_elem_at_(self, idx);
}

/// the elem at 'idx', to write through
/// @copydetails tda_deque_get
[[nodiscard]]
static inline void *tda_deque_get_mut(tda_Deque *self, size_t idx) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(idx < self->len_);

    return tda_deque_elem_at_mut_(self, idx);
}

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
/// @param val the elem to copy in; may be one of this deque's
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

/// copies the front elem to 'out', then drops it
/// @param self asserts the deque is not empty
/// @param[out] out may point into this deque
/// @bigo{1}
TDA_API
void tda_deque_pop_front_to(tda_Deque *self, void *out);

/// copies the back elem to 'out', then drops it
/// @copydetails tda_deque_pop_front_to
TDA_API
void tda_deque_pop_back_to(tda_Deque *self, void *out);

/// puts a copy of 'val' at 'idx', shifting whichever side is shorter
/// @param self the deque
/// @param idx asserts idx <= len; idx == len is push_back
/// @param val the elem to copy in; may be one of this deque's
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}; at most half the elems move, the shorter side, but one at a time, where a
///       vec moves its tail in one memmove: a vec is faster unless the inserts land near
///       the front
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
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, or new_cap * elem.size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_reserve(tda_Deque *self, size_t new_cap);

/// gives back the room above the length
/// @param self a length of 0 releases the block
/// @retval TDA_STATUS_OK on success, including when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the allocator refuses; the deque is unchanged
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_shrink_to_fit(tda_Deque *self);

/// moves the length to 'new_len' at the back, so the front stays put and existing elems
/// keep their indices
/// @param self the deque
/// @param new_len below the length drops from the back, above it appends zeroed elems
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_deque_resize(tda_Deque *self, size_t new_len);

/// drops the elems from 'new_len' on, keeping the capacity; a resize that cannot fail
/// @param self the deque
/// @param new_len at or above the length is a no-op
/// @bigo{1}
TDA_API
void tda_deque_truncate(tda_Deque *self, size_t new_len);

/// exchanges the contents of two deques
/// @param[in,out] self one deque
/// @param[in,out] other must have the same elem layout; may be 'self'. Each
///                      side keeps the allocator its elems came from, so the two may differ
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

/// @name bulk mods
/// @{

/// appends every elem of 'src' at the back, in order
/// @param self the deque
/// @param src must have the same elem layout and must not view this deque: growing may
///            free what it reads
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}; grows once per call by the growth factor, so repeated extends stay
///         amortized O(1) per elem
[[nodiscard]] TDA_API
tda_Status tda_deque_extend(tda_Deque *self, tda_Span src);

/// inserts every elem of 'src' before 'idx', in order, shifting whichever side is shorter
/// @param self the deque
/// @param idx asserts idx <= len; idx == len extends
/// @param src must have the same elem layout and must not view this deque
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}; the shorter side moves once, where a loop of insert moves it once per elem
[[nodiscard]] TDA_API
tda_Status tda_deque_insert_span(tda_Deque *self, size_t idx, tda_Span src);

/// drops 'count' elems from 'idx', closing the gap from whichever side is shorter
/// @param self the deque
/// @param idx asserts idx <= len
/// @param count asserts idx + count <= len
/// @bigo{n}
TDA_API
void tda_deque_remove_range(tda_Deque *self, size_t idx, size_t count);

/// keeps the elems 'pred' passes, in order
/// @param self the deque; the capacity is kept
/// @param pred asked once per elem, front to back; must not change the deque
/// @param ctx handed to 'pred'
/// @bigo{n}
TDA_API
void tda_deque_retain(tda_Deque *self, tda_Pred pred, void *ctx);

/// @}

/// @name print
/// @{

/// writes the elems in ring order as [a, b, c], with no newline
/// @param self the deque
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_deque_fprint(const tda_Deque *self, FILE *stream, tda_FPrint fprint, void *ctx);

/// tda_deque_fprint to stdout, and a newline
/// @param self the deque
/// @param fprint the printer, called once per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_deque_print(const tda_Deque *self, tda_FPrint fprint, void *ctx);

/// @}

/// @name macros
/// @{

/// @cond
// returns 'self' after checking TDA_LAYOUT_OF(T), so a macro evaluates its pointer
// once; _Generic keeps the pointer's constness
[[nodiscard]]
static inline const tda_Deque *tda_deque_typed_(const tda_Deque *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_deque_elem_layout(self), elem));
    return self;
}

[[nodiscard]]
static inline tda_Deque *tda_deque_typed_mut_(tda_Deque *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_deque_elem_layout(self), elem));
    return self;
}

#define TDA_DEQUE_TYPED_(T, self) \
    _Generic((self), const tda_Deque *: tda_deque_typed_, tda_Deque *: tda_deque_typed_mut_)((self), TDA_LAYOUT_OF(T))
/// @endcond

/// tda_deque_init with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param al the allocator
/// @bigo{1}
#define TDA_DEQUE_INIT(T, al) \
    tda_deque_init(TDA_LAYOUT_OF(T), (al))

/// tda_deque_init_len with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param len how many zeroed elems
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @bigo{n}
#define TDA_DEQUE_INIT_LEN(T, len, al, out) \
    tda_deque_init_len((len), TDA_LAYOUT_OF(T), (al), (out))

/// tda_deque_init_cap with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param cap how many elems to make room for
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @bigo{n}
#define TDA_DEQUE_INIT_CAP(T, cap, al, out) \
    tda_deque_init_cap((cap), TDA_LAYOUT_OF(T), (al), (out))

/// tda_deque_from_data with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param data the elems, typechecked as const T *
/// @param len how many to read
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @bigo{n}
#define TDA_DEQUE_FROM_DATA(T, data, len, al, out) \
    tda_deque_from_data((const typeof(T) *){ (data) }, (len), TDA_LAYOUT_OF(T), (al), (out))

/// a new deque from the elems written out: TDA_DEQUE_OF(int32_t, al, &d, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new deque is written
/// @param ... the elems, front to back, as a T initializer list, not empty
/// @bigo{n}
#define TDA_DEQUE_OF(T, al, out, ...)                           \
    tda_deque_from_data(                                        \
        (const typeof(T)[]){ __VA_ARGS__ },                     \
        sizeof((const typeof(T)[]){ __VA_ARGS__ }) / sizeof(T), \
        TDA_LAYOUT_OF(T), (al), (out))

/// tda_deque_front as a const T *
/// @param T the elem type
/// @param self the deque
/// @bigo{1}
#define TDA_DEQUE_FRONT_AS(T, self) \
    ((const typeof(T) *) tda_deque_front(TDA_DEQUE_TYPED_(T, self)))

/// tda_deque_front_mut as a T *
/// @copydetails TDA_DEQUE_FRONT_AS
#define TDA_DEQUE_FRONT_MUT_AS(T, self) \
    ((T *) tda_deque_front_mut(TDA_DEQUE_TYPED_(T, self)))

/// tda_deque_back as a const T *
/// @copydetails TDA_DEQUE_FRONT_AS
#define TDA_DEQUE_BACK_AS(T, self) \
    ((const typeof(T) *) tda_deque_back(TDA_DEQUE_TYPED_(T, self)))

/// tda_deque_back_mut as a T *
/// @copydetails TDA_DEQUE_FRONT_AS
#define TDA_DEQUE_BACK_MUT_AS(T, self) \
    ((T *) tda_deque_back_mut(TDA_DEQUE_TYPED_(T, self)))

/// @cond
// the body of both deque walks; the hidden names come in already unique, and 'self' is
// read once, which also checks T once. The inner loop only binds 'elem', so a break
// leaves just that one; 'brk' is set before the body and cleared only when the body runs
// to its end, so after a break it is still set and stops the walk too
#define TDA_DEQUE_WALK_(DequeT, ElemT, elem, self, get, deque, once, idx, brk)   \
    for (DequeT *deque = (self), **once = &deque; once; once = nullptr)          \
        for (size_t idx = 0, brk = 0; !brk && idx < tda_deque_len(deque); ++idx) \
            for (ElemT *elem = (brk = 1, get(deque, idx)); brk; brk = 0)
/// @endcond

/// walks the deque front to back, binding 'elem' to each elem in turn. A ring has no
/// single view, so this is the walk that copies nothing
/// @param T the elem type
/// @param elem the name of the loop variable, a const T *
/// @param self the deque
/// @note the length is read on every step, so pushing or popping in the body changes what
///       the walk covers, and growing moves the elems out from under 'elem'
/// @note break and continue act on the walk as they would on a plain loop
/// @bigo{n} over the whole walk
#define TDA_DEQUE_FOR_EACH_AS(T, elem, self) \
    TDA_DEQUE_WALK_(                         \
        const tda_Deque,                     \
        const typeof(T),                     \
        elem,                                \
        TDA_DEQUE_TYPED_(T, self),           \
        tda_deque_get,                       \
        TDA_LINE_NAME_(deque_),              \
        TDA_LINE_NAME_(once_),               \
        TDA_LINE_NAME_(idx_),                \
        TDA_LINE_NAME_(brk_)                 \
    )

/// the same walk over elems that may be written through
/// @copydetails TDA_DEQUE_FOR_EACH_AS
#define TDA_DEQUE_FOR_EACH_MUT_AS(T, elem, self) \
    TDA_DEQUE_WALK_(                             \
        tda_Deque,                               \
        T,                                       \
        elem,                                    \
        TDA_DEQUE_TYPED_(T, self),               \
        tda_deque_get_mut,                       \
        TDA_LINE_NAME_(deque_),                  \
        TDA_LINE_NAME_(once_),                   \
        TDA_LINE_NAME_(idx_),                    \
        TDA_LINE_NAME_(brk_)                     \
    )

/// tda_deque_get as a const T *
/// @param T the elem type
/// @param self the deque
/// @param idx the index, counted from the front
/// @bigo{1}
#define TDA_DEQUE_GET_AS(T, self, idx) \
    ((const typeof(T) *) tda_deque_get(TDA_DEQUE_TYPED_(T, self), (idx)))

/// tda_deque_get_mut as a T *
/// @copydetails TDA_DEQUE_GET_AS
#define TDA_DEQUE_GET_MUT_AS(T, self, idx) \
    ((T *) tda_deque_get_mut(TDA_DEQUE_TYPED_(T, self), (idx)))

/// tda_deque_set from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param self the deque
/// @param idx the index, counted from the front
/// @param val the value to copy in
/// @bigo{1}
#define TDA_DEQUE_SET(T, self, idx, val) \
    tda_deque_set(TDA_DEQUE_TYPED_(T, self), (idx), &(T[1]){ (val) }[0])

/// tda_deque_push_front from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param self the deque
/// @param val the value to copy in
/// @bigo{1} amortized
#define TDA_DEQUE_PUSH_FRONT(T, self, val) \
    tda_deque_push_front(TDA_DEQUE_TYPED_(T, self), &(T[1]){ (val) }[0])

/// tda_deque_push_back from a value rather than an address
/// @copydetails TDA_DEQUE_PUSH_FRONT
#define TDA_DEQUE_PUSH_BACK(T, self, val) \
    tda_deque_push_back(TDA_DEQUE_TYPED_(T, self), &(T[1]){ (val) }[0])

/// tda_deque_insert from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param self the deque
/// @param idx the index, counted from the front
/// @param val the value to copy in
/// @bigo{n}
#define TDA_DEQUE_INSERT(T, self, idx, val) \
    tda_deque_insert(TDA_DEQUE_TYPED_(T, self), (idx), &(T[1]){ (val) }[0])

/// tda_deque_pop_front_to with 'out' typechecked as T *
/// @param T the elem type
/// @param self the deque
/// @param[out] out where the elem goes
/// @bigo{1}
#define TDA_DEQUE_POP_FRONT_TO(T, self, out) \
    tda_deque_pop_front_to(TDA_DEQUE_TYPED_(T, self), (T *){ (out) })

/// tda_deque_pop_back_to with 'out' typechecked as T *
/// @copydetails TDA_DEQUE_POP_FRONT_TO
#define TDA_DEQUE_POP_BACK_TO(T, self, out) \
    tda_deque_pop_back_to(TDA_DEQUE_TYPED_(T, self), (T *){ (out) })

/// tda_deque_extend from the elems written out
/// @param T the elem type
/// @param self the deque
/// @param ... the elems, as a T initializer list, not empty
/// @bigo{n}
#define TDA_DEQUE_EXTEND(T, self, ...) \
    tda_deque_extend((self), TDA_SPAN_OF(T, __VA_ARGS__))

/// tda_deque_insert_span from the elems written out
/// @param T the elem type
/// @param self the deque
/// @param idx the index, counted from the front
/// @param ... the elems, as a T initializer list, not empty
/// @bigo{n}
#define TDA_DEQUE_INSERT_SPAN(T, self, idx, ...) \
    tda_deque_insert_span((self), (idx), TDA_SPAN_OF(T, __VA_ARGS__))

/// @}

/// @}
