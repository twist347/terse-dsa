#pragma once

#include "tda/algo/fn.h"
#include "tda/alloc/alloc.h"
#include "tda/core/check.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"
#include "tda/core/status.h"

#include <stddef.h>

/// @file

/// @defgroup ds_vec ds/vec
/// @ingroup ds
/// @brief tda_Vec — an owning array that grows
///
/// One contiguous block with a length and a capacity. The block doubles when full, so
/// push is O(1) amortized. Unlike ds/arr, the length can change.
///
/// Growing moves the elems. Pointers from get_mut and data, and views from to_span_mut,
/// stay valid only until the next op that may reallocate — push, add_one, insert,
/// extend, insert_span, reserve, resize, shrink_to_fit, copy_assign — or a move_assign,
/// which releases the old block. swap moves no elem: a pointer stays valid but points
/// into the other vec. For stable positions use ds/list.
///
/// algo works on the elems in place through tda_vec_to_span_mut; tda_vec_truncate then
/// adopts the length an algorithm leaves.
///
/// Out-of-range indices assert. Only the ops that allocate return a tda_Status.
///
/// @par Example
/// @snippet ds/example_vec.c build
/// @snippet ds/example_vec.c grow
/// @snippet ds/example_vec.c reserved
/// @snippet ds/example_vec.c bulk
/// @snippet ds/example_vec.c algo
/// @snippet ds/example_vec.c allocators
/// @{

/// Owning array that grows. A value the caller holds wherever it likes, on the stack or
/// inside a struct: an init or a from_ fills it in, tda_vec_deinit releases its block.
/// `= {}` is the inert state, which tda_vec_deinit accepts and every other op asserts.
///
/// The fields belong to the vec: read and change them only through the functions here.
///
/// @warning a copy of the struct is a second owner of the same block: releasing both
///          frees it twice, and growing one leaves the other dangling. Pass a pointer,
///          and hand a vec over with tda_vec_move_assign.
typedef struct tda_Vec {
    void *data_;      ///< the block; null while cap is 0
    size_t len_;      ///< how many elems it holds
    size_t cap_;      ///< how many fit before the block grows
    tda_Layout elem_; ///< the layout of one elem
    tda_Al *al_;      ///< where the block comes from; moves along with the vec
} tda_Vec;

/// @name lifetime
/// @{

/// an empty vec that owns no block yet; allocates nothing, so it cannot fail
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al used for every later allocation
/// @param[out] out the new vec
/// @bigo{1}
TDA_API
void tda_vec_init(tda_Layout elem, tda_Al *al, tda_Vec *out);

/// a vec of 'len' zeroed elems, with the capacity to match
/// @param len how many elems
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new vec, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or len * elem.size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_init_len(size_t len, tda_Layout elem, tda_Al *al, tda_Vec *out);

/// an empty vec with room for 'cap' elems before the first growth
/// @param cap how many elems to make room for
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new vec, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or cap * elem.size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_init_cap(size_t cap, tda_Layout elem, tda_Al *al, tda_Vec *out);

/// a vec holding a copy of 'len' elems read from 'data'
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al the allocator
/// @param[out] out the new vec, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or len * elem.size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_from_data(const void *data, size_t len, tda_Layout elem, tda_Al *al, tda_Vec *out);

/// a vec holding a copy of the elems 's' views
/// @param s the view to copy; the elem layout is the span's
/// @param al the allocator
/// @param[out] out the new vec, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_from_span(tda_Span s, tda_Al *al, tda_Vec *out);

/// releases the block and leaves the vec inert, `= {}`
/// @param self a vec, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_vec_deinit(tda_Vec *self);

/// @}

/// @name copy
/// @{

/// a new vec with the same elems, on the same allocator
/// @param self the vec to copy
/// @param[out] out the new vec, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_copy(const tda_Vec *self, tda_Vec *out);

/// a new vec with the same elems, on 'al'
/// @param self the vec to copy
/// @param al the allocator for the copy
/// @param[out] out the new vec, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_copy_with(const tda_Vec *self, tda_Al *al, tda_Vec *out);

/// overwrites the elems of 'self' with those of 'other', growing the block if needed
/// @param[in,out] self must have the same elem layout; keeps its allocator
/// @param other the vec to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving 'self' as it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_copy_assign(tda_Vec *self, const tda_Vec *other);

/// releases the elems of 'self' and hands it everything 'other' holds, its allocator
/// included, leaving 'other' empty on that allocator
/// @param[in,out] self must have the same elem layout
/// @param[in,out] other emptied and still usable; may be 'self'
/// @bigo{1}
TDA_API
void tda_vec_move_assign(tda_Vec *self, tda_Vec *other);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems, byte for byte
/// @param a one vec
/// @param b must have the same elem layout (asserted); a different length gives false
/// @return whether the lengths match and the bytes do
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_vec_eq(const tda_Vec *a, const tda_Vec *b);

/// whether the two hold equal elems under 'eq'
/// @param a one vec
/// @param b must have the same elem layout
/// @param eq asked of each pair until one fails
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_vec_eq_by(const tda_Vec *a, const tda_Vec *b, tda_Eq eq, void *ctx);

/// @}

/// @name info
/// @{

/// how many elems the vec holds
/// @param self the vec
/// @return the length
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_vec_len(const tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return self->len_;
}

/// how many elems fit before the block must grow
/// @param self the vec
/// @return the capacity
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_vec_cap(const tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return self->cap_;
}

/// the layout of one elem, as the vec was built with
/// @param self the vec
/// @return the size and the alignment; every elem is aligned to it
/// @bigo{1}
[[nodiscard]]
static inline tda_Layout tda_vec_elem_layout(const tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return self->elem_;
}

/// how many bytes the elems take
/// @param self the vec
/// @return len * elem.size
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_vec_bytes(const tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return self->len_ * self->elem_.size;
}

/// the allocator the vec was built with
/// @param self the vec
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]]
static inline tda_Al *tda_vec_al(const tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return self->al_;
}

/// @}

/// @name access
/// @{

/// the front elem
/// @param self asserts the vec is not empty
/// @return valid until the next op that may reallocate
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_vec_front(const tda_Vec *self) {
    assert(self);
    assert(self->al_);
    TDA_EXPECT(self->len_ > 0);

    return self->data_;
}

/// the front elem, to write through
/// @copydetails tda_vec_front
[[nodiscard]]
static inline void *tda_vec_front_mut(tda_Vec *self) {
    assert(self);
    assert(self->al_);
    TDA_EXPECT(self->len_ > 0);

    return self->data_;
}

/// the back elem
/// @copydetails tda_vec_front
[[nodiscard]]
static inline const void *tda_vec_back(const tda_Vec *self) {
    assert(self);
    assert(self->al_);
    TDA_EXPECT(self->len_ > 0);

    return (const unsigned char *) self->data_ + (self->len_ - 1) * self->elem_.size;
}

/// the back elem, to write through
/// @copydetails tda_vec_front
[[nodiscard]]
static inline void *tda_vec_back_mut(tda_Vec *self) {
    assert(self);
    assert(self->al_);
    TDA_EXPECT(self->len_ > 0);

    return (unsigned char *) self->data_ + (self->len_ - 1) * self->elem_.size;
}

/// the elem at 'idx'
/// @param self the vec
/// @param idx asserts idx < len
/// @return valid until the next op that may reallocate
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_vec_get(const tda_Vec *self, size_t idx) {
    assert(self);
    assert(self->al_);
    TDA_EXPECT(idx < self->len_);

    return (const unsigned char *) self->data_ + idx * self->elem_.size;
}

/// the elem at 'idx', to write through
/// @copydetails tda_vec_get
[[nodiscard]]
static inline void *tda_vec_get_mut(tda_Vec *self, size_t idx) {
    assert(self);
    assert(self->al_);
    TDA_EXPECT(idx < self->len_);

    return (unsigned char *) self->data_ + idx * self->elem_.size;
}

/// overwrites the elem at 'idx' with a copy of 'val'
/// @param self the vec
/// @param idx asserts idx < len
/// @param val the elem to copy in
/// @bigo{1}
TDA_API
void tda_vec_set(tda_Vec *self, size_t idx, const void *val);

/// the block itself
/// @param self the vec
/// @return the first elem, or null while there is no block; valid until the next op
///         that may reallocate
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_vec_data(const tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return self->data_;
}

/// the block itself, to write through
/// @copydetails tda_vec_data
[[nodiscard]]
static inline void *tda_vec_data_mut(tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return self->data_;
}

/// @}

/// @name mods
/// @{

/// appends a copy of 'val'
/// @param self the vec
/// @param val must not point into this vec: growing would move it
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{1} amortized
[[nodiscard]] TDA_API
tda_Status tda_vec_push(tda_Vec *self, const void *val);

/// appends a copy of 'val' into room already reserved, so it cannot fail: one
/// tda_vec_reserve before a loop, and the pushes inside need no status
/// @param self the vec; asserts len < cap
/// @param val the elem to copy in; may point into this vec, since nothing moves
/// @bigo{1}
static inline void tda_vec_push_assume_cap(tda_Vec *self, const void *val) {
    assert(self);
    assert(self->al_);
    assert(val);
    TDA_EXPECT(self->len_ < self->cap_);

    // the new slot lies past every elem, so 'val' cannot overlap it
    memcpy((unsigned char *) self->data_ + self->len_ * self->elem_.size, val, self->elem_.size);
    ++self->len_;
}

/// grows the vec by one elem and hands out its slot, to fill in place rather than copy
/// a value in
/// @param self the vec
/// @param[out] out the new elem's slot, written only on success; its bytes are
///                 unspecified until the caller writes them, and it is valid until the
///                 next op that may reallocate
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, leaving the vec as it was
/// @bigo{1} amortized
[[nodiscard]] TDA_API
tda_Status tda_vec_add_one(tda_Vec *self, void **out);

/// grows the vec by one elem into room already reserved and hands out its slot, so it
/// cannot fail
/// @param self the vec; asserts len < cap
/// @return the new elem's slot; its bytes are unspecified until the caller writes them
/// @bigo{1}
[[nodiscard]]
static inline void *tda_vec_add_one_assume_cap(tda_Vec *self) {
    assert(self);
    assert(self->al_);
    TDA_EXPECT(self->len_ < self->cap_);

    return (unsigned char *) self->data_ + self->len_++ * self->elem_.size;
}

/// drops the back elem, keeping the capacity
/// @param self asserts the vec is not empty
/// @bigo{1}
static inline void tda_vec_pop(tda_Vec *self) {
    assert(self);
    assert(self->al_);
    TDA_EXPECT(self->len_ > 0);

    --self->len_;
}

/// copies the back elem to 'out', then drops it
/// @param self asserts the vec is not empty
/// @param[out] out may point into this vec
/// @bigo{1}
TDA_API
void tda_vec_pop_to(tda_Vec *self, void *out);

/// puts a copy of 'val' at 'idx', moving the elems from there on one place up
/// @param self the vec
/// @param idx asserts idx <= len; idx == len appends
/// @param val must not point into this vec
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_insert(tda_Vec *self, size_t idx, const void *val);

/// drops the elem at 'idx' and closes the gap
/// @param self the vec
/// @param idx asserts idx < len
/// @bigo{n}
TDA_API
void tda_vec_remove(tda_Vec *self, size_t idx);

/// drops the elem at 'idx' and moves the back elem into its place; the order is lost
/// @param self the vec
/// @param idx asserts idx < len
/// @bigo{1}
TDA_API
void tda_vec_swap_remove(tda_Vec *self, size_t idx);

/// drops every elem, keeping the block
/// @param self the vec
/// @bigo{1}
static inline void tda_vec_clear(tda_Vec *self) {
    assert(self);
    assert(self->al_);

    self->len_ = 0;
}

/// makes room for 'new_cap' elems
/// @param self the vec
/// @param new_cap a capacity at or below the one it has is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow, or new_cap * elem.size
///         overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_reserve(tda_Vec *self, size_t new_cap);

/// gives back the room above the length
/// @param self a length of 0 releases the block
/// @retval TDA_STATUS_OK on success, including when there was nothing to give back
/// @retval TDA_STATUS_ERR_NO_MEM when the smaller block is refused; the vec is unchanged
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_shrink_to_fit(tda_Vec *self);

/// moves the length to 'new_len', zeroing what it grows over
/// @param self the vec
/// @param new_len below the length drops the tail, above it appends zeroed elems
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_vec_resize(tda_Vec *self, size_t new_len);

/// drops the elems from 'new_len' on, keeping the capacity; a resize that cannot fail
/// @param self the vec
/// @param new_len at or above the length is a no-op
/// @bigo{1}
TDA_API
void tda_vec_truncate(tda_Vec *self, size_t new_len);

/// exchanges the contents of two vecs
/// @param[in,out] self one vec
/// @param[in,out] other must have the same elem layout; may be 'self'. Each block goes
///                      along with its allocator, so the two may differ
/// @bigo{1}
TDA_API
void tda_vec_swap(tda_Vec *self, tda_Vec *other);

/// exchanges the elems at 'i' and 'j'
/// @param self the vec
/// @param i asserts i < len
/// @param j asserts j < len; i == j is a no-op
/// @bigo{1}
TDA_API
void tda_vec_swap_elems(tda_Vec *self, size_t i, size_t j);

/// @}

/// @name bulk mods
/// @{

/// appends every elem of 'src', in order
/// @param self the vec
/// @param src must have the same elem layout and must not view this vec: growing may free
///            what it reads
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}; grows once per call by the growth factor, so repeated extends stay
///         amortized O(1) per elem
[[nodiscard]] TDA_API
tda_Status tda_vec_extend(tda_Vec *self, tda_Span src);

/// inserts every elem of 'src' before 'idx', in order
/// @param self the vec
/// @param idx asserts idx <= len; idx == len extends
/// @param src must have the same elem layout and must not view this vec
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot grow
/// @bigo{n}; the tail moves once, where a loop of insert would cost O(len * src.len)
[[nodiscard]] TDA_API
tda_Status tda_vec_insert_span(tda_Vec *self, size_t idx, tda_Span src);

/// drops 'count' elems from 'idx' and closes the gap
/// @param self the vec
/// @param idx asserts idx <= len
/// @param count asserts idx + count <= len
/// @bigo{n}
TDA_API
void tda_vec_remove_range(tda_Vec *self, size_t idx, size_t count);

/// keeps the elems 'pred' passes, in order
/// @param self the vec; the capacity is kept
/// @param pred asked once per elem, front to back; must not change the vec
/// @param ctx handed to 'pred'
/// @bigo{n}
TDA_API
void tda_vec_retain(tda_Vec *self, tda_Pred pred, void *ctx);

/// @}

/// @name to span
/// @{

/// a writable view of the elems, for algo
/// @param self the vec
/// @return valid until the next op that may reallocate
/// @bigo{1}
[[nodiscard]]
static inline tda_SpanMut tda_vec_to_span_mut(tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return tda_span_from_data_mut(self->data_, self->len_, self->elem_);
}

/// a read-only view of the elems
/// @copydetails tda_vec_to_span_mut
[[nodiscard]]
static inline tda_Span tda_vec_to_span(const tda_Vec *self) {
    assert(self);
    assert(self->al_);

    return tda_span_from_data(self->data_, self->len_, self->elem_);
}

/// @}

/// @name print
/// @{

/// writes the elems as [a, b, c] and a newline
/// @param self the vec
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_vec_fprint(const tda_Vec *self, FILE *stream, tda_FPrint fprint);

/// tda_vec_fprint to stdout
/// @param self the vec
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_vec_print(const tda_Vec *self, tda_FPrint fprint);

/// @}

/// @name macros
/// @{

/// @cond
// returns 'self' after checking TDA_LAYOUT_OF(T), so a macro evaluates its pointer
// once; _Generic keeps the pointer's constness
[[nodiscard]]
static inline const tda_Vec *tda_vec_typed_(const tda_Vec *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(self), elem));
    return self;
}

[[nodiscard]]
static inline tda_Vec *tda_vec_typed_mut_(tda_Vec *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_vec_elem_layout(self), elem));
    return self;
}

#define TDA_VEC_TYPED_(T, self) \
    _Generic((self), const tda_Vec *: tda_vec_typed_, tda_Vec *: tda_vec_typed_mut_)((self), TDA_LAYOUT_OF(T))
/// @endcond

/// tda_vec_init with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new vec is written
/// @bigo{1}
#define TDA_VEC_INIT(T, al, out) \
    tda_vec_init(TDA_LAYOUT_OF(T), (al), (out))

/// tda_vec_init_len with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param len how many zeroed elems
/// @param al the allocator
/// @param[out] out where the new vec is written
/// @bigo{n}
#define TDA_VEC_INIT_LEN(T, len, al, out) \
    tda_vec_init_len((len), TDA_LAYOUT_OF(T), (al), (out))

/// tda_vec_init_cap with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param cap how many elems to make room for
/// @param al the allocator
/// @param[out] out where the new vec is written
/// @bigo{n}
#define TDA_VEC_INIT_CAP(T, cap, al, out) \
    tda_vec_init_cap((cap), TDA_LAYOUT_OF(T), (al), (out))

/// tda_vec_from_data with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param data the elems, typechecked as const T *
/// @param len how many to read
/// @param al the allocator
/// @param[out] out where the new vec is written
/// @bigo{n}
#define TDA_VEC_FROM_DATA(T, data, len, al, out) \
    tda_vec_from_data((const T *){ (data) }, (len), TDA_LAYOUT_OF(T), (al), (out))

/// a new vec from the elems written out: TDA_VEC_OF(int32_t, al, &v, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new vec is written
/// @param ... the elems, as a T initializer list, not empty
/// @bigo{n}
#define TDA_VEC_OF(T, al, out, ...)                     \
    tda_vec_from_data(                                  \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        TDA_LAYOUT_OF(T), (al), (out))

/// tda_vec_front as a const T *
/// @param T the elem type
/// @param self the vec
/// @bigo{1}
#define TDA_VEC_FRONT_AS(T, self) \
    ((const T *) tda_vec_front(TDA_VEC_TYPED_(T, self)))

/// tda_vec_front_mut as a T *
/// @copydetails TDA_VEC_FRONT_AS
#define TDA_VEC_FRONT_MUT_AS(T, self) \
    ((T *) tda_vec_front_mut(TDA_VEC_TYPED_(T, self)))

/// tda_vec_back as a const T *
/// @copydetails TDA_VEC_FRONT_AS
#define TDA_VEC_BACK_AS(T, self) \
    ((const T *) tda_vec_back(TDA_VEC_TYPED_(T, self)))

/// tda_vec_back_mut as a T *
/// @copydetails TDA_VEC_FRONT_AS
#define TDA_VEC_BACK_MUT_AS(T, self) \
    ((T *) tda_vec_back_mut(TDA_VEC_TYPED_(T, self)))

/// tda_vec_get as a const T *
/// @param T the elem type
/// @param self the vec
/// @param idx the index
/// @bigo{1}
#define TDA_VEC_GET_AS(T, self, idx) \
    ((const T *) tda_vec_get(TDA_VEC_TYPED_(T, self), (idx)))

/// tda_vec_get_mut as a T *
/// @copydetails TDA_VEC_GET_AS
#define TDA_VEC_GET_MUT_AS(T, self, idx) \
    ((T *) tda_vec_get_mut(TDA_VEC_TYPED_(T, self), (idx)))

/// tda_vec_set from a value rather than an address
/// @param T the elem type; scalars only, as 'val' becomes a compound literal
/// @param self the vec
/// @param idx the index
/// @param val the value to copy in
/// @bigo{1}
#define TDA_VEC_SET(T, self, idx, val) \
    tda_vec_set(TDA_VEC_TYPED_(T, self), (idx), &(T){ (val) })

/// tda_vec_push from a value rather than an address
/// @param T the elem type; scalars only, as 'val' becomes a compound literal
/// @param self the vec
/// @param val the value to copy in
/// @bigo{1} amortized
#define TDA_VEC_PUSH(T, self, val) \
    tda_vec_push(TDA_VEC_TYPED_(T, self), &(T){ (val) })

/// tda_vec_push_assume_cap from a value rather than an address
/// @param T the elem type; scalars only, as 'val' becomes a compound literal
/// @param self the vec
/// @param val the value to copy in
/// @bigo{1}
#define TDA_VEC_PUSH_ASSUME_CAP(T, self, val) \
    tda_vec_push_assume_cap(TDA_VEC_TYPED_(T, self), &(T){ (val) })

/// tda_vec_add_one_assume_cap as a T *
/// @param T the elem type
/// @param self the vec
/// @bigo{1}
#define TDA_VEC_ADD_ONE_ASSUME_CAP_AS(T, self) \
    ((T *) tda_vec_add_one_assume_cap(TDA_VEC_TYPED_(T, self)))

/// tda_vec_pop_to with 'out' typechecked as T *
/// @param T the elem type
/// @param self the vec
/// @param[out] out where the elem goes
/// @bigo{1}
#define TDA_VEC_POP_TO(T, self, out) \
    tda_vec_pop_to(TDA_VEC_TYPED_(T, self), (T *){ (out) })

/// tda_vec_insert from a value rather than an address
/// @param T the elem type; scalars only, as 'val' becomes a compound literal
/// @param self the vec
/// @param idx the index
/// @param val the value to copy in
/// @bigo{n}
#define TDA_VEC_INSERT(T, self, idx, val) \
    tda_vec_insert(TDA_VEC_TYPED_(T, self), (idx), &(T){ (val) })

/// tda_vec_extend from the elems written out
/// @param T the elem type
/// @param self the vec
/// @param ... the elems, as a T initializer list, not empty
/// @bigo{n}
#define TDA_VEC_EXTEND(T, self, ...) \
    tda_vec_extend((self), TDA_SPAN_OF(T, __VA_ARGS__))

/// tda_vec_insert_span from the elems written out
/// @param T the elem type
/// @param self the vec
/// @param idx the index
/// @param ... the elems, as a T initializer list, not empty
/// @bigo{n}
#define TDA_VEC_INSERT_SPAN(T, self, idx, ...) \
    tda_vec_insert_span((self), (idx), TDA_SPAN_OF(T, __VA_ARGS__))

/// @}

/// @}
