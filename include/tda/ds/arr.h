#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/check.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/core/status.h"

#include <stddef.h>

/// @file

/// @defgroup ds_arr ds/arr
/// @ingroup ds
/// @brief tda_Arr — an owning array whose length is set when it is built
///
/// One block, allocated once; no op changes the length of a live arr. This is ds/vec
/// without growth.
///
/// The length changes only when the whole arr is replaced: copy_assign and move_assign
/// overwrite one, swap exchanges two. These are also the only ops that move the block, so
/// only they invalidate pointers from get_mut and data and views from to_span_mut.
///
/// algo sorts and fills the elems in place through tda_arr_to_span_mut.
///
/// Out-of-range indices assert. Only the ops that allocate return a tda_Status.
///
/// Elems are copied in as bytes and freed with the block. Whatever an elem points to is
/// the caller's to release.
///
/// An arr knows the layout of its elem, size and alignment, and asks the allocator for a
/// block aligned to it. The typed macros pass TDA_LAYOUT_OF(T).
///
/// The typed macros write 'const T', so an elem type already declared const needs its
/// own typedef.
///
/// @par Example
/// @snippet ds/example_arr.c build
/// @snippet ds/example_arr.c compare
/// @snippet ds/example_arr.c algo
/// @snippet ds/example_arr.c access
/// @snippet ds/example_arr.c copy
/// @{

/// Owning array whose length is set when it is built. A value the caller holds wherever
/// it likes: an init or a from_ fills it in, tda_arr_deinit releases its block. `= {}`
/// is the inert state, which tda_arr_deinit accepts and every other op asserts.
///
/// The fields belong to the arr: read and change them only through the functions here.
///
/// @warning a copy of the struct is a second owner of the same block: releasing both
///          frees it twice. Pass a pointer, and hand an arr over with
///          tda_arr_move_assign.
typedef struct tda_Arr {
    void *data_;      ///< the block; null while len is 0
    size_t len_;      ///< how many elems it holds
    tda_Layout elem_; ///< the layout of one elem
    tda_Al al_;       ///< where the block comes from; moves along with the arr
} tda_Arr;

/// @name lifetime
/// @{

/// a new arr of 'len' zeroed elems
/// @param len how many elems; 0 gives an arr with no block
/// @param elem the layout of one elem, asserted to be a type's: TDA_LAYOUT_OF gives it
/// @param al used for every later allocation
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem.size overflows
/// @bigo{n}, for zeroing the block
[[nodiscard]] TDA_API
tda_Status tda_arr_init_len(size_t len, tda_Layout elem, tda_Al al, tda_Arr *out);

/// a new arr holding a copy of 'len' elems read from 'data'
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem the layout of one elem, asserted to be a type's
/// @param al the allocator
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated, or
///         len * elem.size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_from_data(const void *data, size_t len, tda_Layout elem, tda_Al al, tda_Arr *out);

/// a new arr holding a copy of the elems 's' views
/// @param s the view to copy; the elem layout is the span's
/// @param al the allocator; need not be the one 's' points into
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_from_span(tda_Span s, tda_Al al, tda_Arr *out);

/// releases the block and leaves the arr inert, `= {}`
/// @param self an arr, or an inert one, for which this is a no-op; what the elems point
///             to is not released
/// @bigo{1}
TDA_API
void tda_arr_deinit(tda_Arr *self);

/// @}

/// @name copy
/// @{

/// a new arr with the same elems, on the same allocator
/// @param self the arr to copy
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_copy(const tda_Arr *self, tda_Arr *out);

/// a new arr with the same elems, on 'al'
/// @param self the arr to copy
/// @param al the allocator for the copy
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_copy_with(const tda_Arr *self, tda_Al al, tda_Arr *out);

/// overwrites the elems of 'self' with those of 'other', resizing the block if the
/// lengths differ
/// @param[in,out] self must have the same elem layout; keeps its allocator
/// @param other the arr to copy from; may be 'self'
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be resized; 'self' is unchanged
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_copy_assign(tda_Arr *self, const tda_Arr *other);

/// releases the elems of 'self' and hands it everything 'other' holds, its allocator
/// included, leaving 'other' empty on that allocator
/// @param[in,out] self must have the same elem layout
/// @param[in,out] other emptied and still usable; may be 'self'
/// @bigo{1}
TDA_API
void tda_arr_move_assign(tda_Arr *self, tda_Arr *other);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems, byte for byte
/// @param a one arr
/// @param b must have the same elem layout (asserted); a different length gives false
/// @return whether the lengths match and the bytes do; as memcmp, it tells -0.0 from
///         +0.0 and compares struct padding
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_arr_eq(const tda_Arr *a, const tda_Arr *b);

/// whether the two hold equal elems under 'eq'
/// @param a one arr
/// @param b must have the same elem layout
/// @param eq asked of each pair until one fails
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_arr_eq_by(const tda_Arr *a, const tda_Arr *b, tda_Eq eq, void *ctx);

/// @}

/// @name info
/// @{

/// how many elems the arr holds; changed only by copy_assign, move_assign and swap
/// @param self the arr
/// @return the length
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_arr_len(const tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);

    return self->len_;
}

/// the layout of one elem, as the arr was built with
/// @param self the arr
/// @return the size and the alignment; the block is aligned to it
/// @bigo{1}
[[nodiscard]]
static inline tda_Layout tda_arr_elem_layout(const tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);

    return self->elem_;
}

/// the size of the block the arr owns
/// @param self the arr
/// @return len * elem.size
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_arr_bytes(const tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);

    return self->len_ * self->elem_.size;
}

/// the allocator the arr was built with
/// @param self the arr
/// @return the view it was built with
/// @bigo{1}
[[nodiscard]]
static inline tda_Al tda_arr_al(const tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);

    return self->al_;
}

/// @}

/// @name access
/// @{

/// the front elem
/// @param self asserts the arr is not empty
/// @return valid until the arr is released, swapped or assigned to
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_arr_front(const tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(self->len_ > 0);

    return self->data_;
}

/// the front elem, to write through
/// @copydetails tda_arr_front
[[nodiscard]]
static inline void *tda_arr_front_mut(tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(self->len_ > 0);

    return self->data_;
}

/// the back elem
/// @copydetails tda_arr_front
[[nodiscard]]
static inline const void *tda_arr_back(const tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(self->len_ > 0);

    return (const unsigned char *) self->data_ + (self->len_ - 1) * self->elem_.size;
}

/// the back elem, to write through
/// @copydetails tda_arr_front
[[nodiscard]]
static inline void *tda_arr_back_mut(tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(self->len_ > 0);

    return (unsigned char *) self->data_ + (self->len_ - 1) * self->elem_.size;
}

/// the elem at 'idx'
/// @param self the arr
/// @param idx asserts idx < len
/// @return valid until the arr is released, swapped or assigned to
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_arr_get(const tda_Arr *self, size_t idx) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(idx < self->len_);

    return (const unsigned char *) self->data_ + idx * self->elem_.size;
}

/// the elem at 'idx', to write through
/// @copydetails tda_arr_get
[[nodiscard]]
static inline void *tda_arr_get_mut(tda_Arr *self, size_t idx) {
    assert(self);
    assert(self->al_.vt);
    TDA_EXPECT(idx < self->len_);

    return (unsigned char *) self->data_ + idx * self->elem_.size;
}

/// overwrites the elem at 'idx' with a copy of 'val'
/// @param self the arr
/// @param idx asserts idx < len
/// @param val the elem to copy in
/// @bigo{1}
TDA_API
void tda_arr_set(tda_Arr *self, size_t idx, const void *val);

/// the block itself
/// @param self the arr
/// @return the first elem, or null while the arr is empty; valid until the arr is
///         released, swapped or assigned to
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_arr_data(const tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);

    return self->data_;
}

/// the block itself, to write through
/// @copydetails tda_arr_data
[[nodiscard]]
static inline void *tda_arr_data_mut(tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);

    return self->data_;
}

/// @}

/// @name mods
/// @{

/// exchanges the contents of two arrs
/// @param[in,out] self one arr
/// @param[in,out] other must have the same elem layout; may be 'self'. Each
///                      side keeps the allocator its elems came from, so the two may differ
/// @bigo{1}
TDA_API
void tda_arr_swap(tda_Arr *self, tda_Arr *other);

/// exchanges two elems in place
/// @param self the arr
/// @param i asserts i < len
/// @param j asserts j < len; i == j is a no-op
/// @bigo{1}
TDA_API
void tda_arr_swap_elems(tda_Arr *self, size_t i, size_t j);

/// @}

/// @name to span
/// @{

/// a writable view of the elems, for algo
/// @param self the arr
/// @return valid until the arr is released, swapped or assigned to
/// @bigo{1}
[[nodiscard]]
static inline tda_SpanMut tda_arr_to_span_mut(tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);

    return tda_span_from_data_mut(self->data_, self->len_, self->elem_);
}

/// a read-only view of the elems
/// @copydetails tda_arr_to_span_mut
[[nodiscard]]
static inline tda_Span tda_arr_to_span(const tda_Arr *self) {
    assert(self);
    assert(self->al_.vt);

    return tda_span_from_data(self->data_, self->len_, self->elem_);
}

/// @}

/// @name print
/// @{

/// writes the elems as [a, b, c], with no newline
/// @param self the arr
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_arr_fprint(const tda_Arr *self, FILE *stream, tda_FPrint fprint, void *ctx);

/// tda_arr_fprint to stdout, and a newline
/// @param self the arr
/// @param fprint the printer, called once per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
TDA_API
int tda_arr_print(const tda_Arr *self, tda_FPrint fprint, void *ctx);

/// @}

/// @name macros
/// @{

/// @cond
// returns 'self' after checking TDA_LAYOUT_OF(T), so a macro evaluates its pointer
// once; _Generic keeps the pointer's constness
[[nodiscard]]
static inline const tda_Arr *tda_arr_typed_(const tda_Arr *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_arr_elem_layout(self), elem));
    return self;
}

[[nodiscard]]
static inline tda_Arr *tda_arr_typed_mut_(tda_Arr *self, [[maybe_unused]] tda_Layout elem) {
    TDA_EXPECT(tda_layout_eq(tda_arr_elem_layout(self), elem));
    return self;
}

#define TDA_ARR_TYPED_(T, self) \
    _Generic((self), const tda_Arr *: tda_arr_typed_, tda_Arr *: tda_arr_typed_mut_)((self), TDA_LAYOUT_OF(T))
/// @endcond

/// tda_arr_init_len with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param len how many elems
/// @param al the allocator
/// @param[out] out where the new arr is written
/// @bigo{n}
#define TDA_ARR_INIT_LEN(T, len, al, out) \
    tda_arr_init_len((len), TDA_LAYOUT_OF(T), (al), (out))

/// tda_arr_from_data with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param data the elems, typechecked as const T *
/// @param len how many to read
/// @param al the allocator
/// @param[out] out where the new arr is written
/// @bigo{n}
#define TDA_ARR_FROM_DATA(T, data, len, al, out) \
    tda_arr_from_data((const T *){ (data) }, (len), TDA_LAYOUT_OF(T), (al), (out))

/// a new arr from the elems written out: TDA_ARR_OF(int32_t, al, &a, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new arr is written
/// @param ... the elems, as a T initializer list, not empty
/// @bigo{n}
#define TDA_ARR_OF(T, al, out, ...)                     \
    tda_arr_from_data(                                  \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        TDA_LAYOUT_OF(T), (al), (out))

/// tda_arr_front as a const T *
/// @param T the elem type
/// @param self the arr
/// @bigo{1}
#define TDA_ARR_FRONT_AS(T, self) \
    ((const T *) tda_arr_front(TDA_ARR_TYPED_(T, self)))

/// tda_arr_front_mut as a T *
/// @copydetails TDA_ARR_FRONT_AS
#define TDA_ARR_FRONT_MUT_AS(T, self) \
    ((T *) tda_arr_front_mut(TDA_ARR_TYPED_(T, self)))

/// tda_arr_back as a const T *
/// @copydetails TDA_ARR_FRONT_AS
#define TDA_ARR_BACK_AS(T, self) \
    ((const T *) tda_arr_back(TDA_ARR_TYPED_(T, self)))

/// tda_arr_back_mut as a T *
/// @copydetails TDA_ARR_FRONT_AS
#define TDA_ARR_BACK_MUT_AS(T, self) \
    ((T *) tda_arr_back_mut(TDA_ARR_TYPED_(T, self)))

/// tda_arr_get as a const T *
/// @param T the elem type
/// @param self the arr
/// @param idx the index
/// @bigo{1}
#define TDA_ARR_GET_AS(T, self, idx) \
    ((const T *) tda_arr_get(TDA_ARR_TYPED_(T, self), (idx)))

/// tda_arr_get_mut as a T *
/// @copydetails TDA_ARR_GET_AS
#define TDA_ARR_GET_MUT_AS(T, self, idx) \
    ((T *) tda_arr_get_mut(TDA_ARR_TYPED_(T, self), (idx)))

/// tda_arr_set from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param self the arr
/// @param idx the index
/// @param val the value to copy in
/// @bigo{1}
#define TDA_ARR_SET(T, self, idx, val) \
    tda_arr_set(TDA_ARR_TYPED_(T, self), (idx), &(T[1]){ (val) }[0])

/// @}

/// @}
