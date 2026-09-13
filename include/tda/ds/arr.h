#pragma once

#include "tda/alloc/alloc.h"
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
/// The elems live in one block, allocated once, and no operation changes how many there
/// are. This is ds/vec minus the growth.
///
/// The length moves only when the whole arr is replaced: tda_arr_copy_assign overwrites
/// one, tda_arr_swap exchanges two. Those are also the only two ops that move the block,
/// and so the only two that invalidate what tda_arr_get_mut, tda_arr_data and
/// tda_arr_to_span_mut handed out.
///
/// The view is writable, so algo sorts and fills in place through it. An index out of
/// range asserts; the ops that return a tda_Status are the ones that allocate.
///
/// An elem is bytes: the arr copies them in and frees them with the block. Whatever an
/// elem points to is the caller's to release.
///
/// The typed macros write 'const T', so an elem type already spelled with const needs a
/// typedef of its own.
///
/// @par Example
/// @snippet ds/example_arr.c build
/// @snippet ds/example_arr.c compare
/// @snippet ds/example_arr.c algo
/// @snippet ds/example_arr.c access
/// @snippet ds/example_arr.c copy
/// @{

/// Owning array whose length is set when it is built.
/// An opaque handle: it comes from one of the constructors and goes back to tda_arr_drop
typedef struct tda_Arr tda_Arr;

/// @name lifetime
/// @{

/// a new arr of 'len' zeroed elems
/// @param len how many elems the arr will hold; 0 gives an arr that owns no block
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator, kept for everything after
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated, or
///         len * elem_size overflows
/// @bigo{n} — the block is zeroed
[[nodiscard]] TDA_API
tda_Status tda_arr_new_len(size_t len, size_t elem_size, tda_Al *al, tda_Arr **out);

/// a new arr holding a copy of 'len' elems read from 'data'
/// @param data the elems to copy in; may be null only when len is 0
/// @param len how many elems to read
/// @param elem_size the size of one elem, asserted greater than 0
/// @param al the allocator
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated, or
///         len * elem_size overflows
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_from_data(const void *data, size_t len, size_t elem_size, tda_Al *al, tda_Arr **out);

/// a new arr holding a copy of what 's' views, taking its len and elem_size
/// @param s the view to copy
/// @param al the allocator; unrelated to where 's' points, the elems are copied out of it
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_from_span(tda_Span s, tda_Al *al, tda_Arr **out);

/// releases the block and the arr through the allocator it was built with
/// @param self null is a no-op, so this is safe on a partly built object; what the elems
///             point to is not released
/// @bigo{1}
TDA_API
void tda_arr_drop(tda_Arr *self);

/// @}

/// @name copy
/// @{

/// a new arr with the same elems, on the same allocator
/// @param self the arr to copy
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_copy(const tda_Arr *self, tda_Arr **out);

/// a new arr with the same elems, on 'al'
/// @param self the arr to copy
/// @param al where the copy lives; tda_arr_copy is this one with the allocator of 'self'
/// @param[out] out the new arr, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the header or the block cannot be allocated
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_copy_with(const tda_Arr *self, tda_Al *al, tda_Arr **out);

/// overwrites the elems of 'other' with those of 'self', resizing its block when the two
/// lengths differ
/// @param self the arr to copy from
/// @param[in,out] other must have the same elem_size; keeps its own allocator, and
///                      'self' == 'other' is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the block cannot be resized, leaving 'other' as
///         it was
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_arr_copy_assign(const tda_Arr *self, tda_Arr *other);

/// moves the elems of 'self' into 'other', leaving 'self' empty
/// @param[in,out] self the arr to move from; emptied on success and still usable, on
///                     its own allocator
/// @param[in,out] other must have the same elem_size; releases what it held and keeps
///                      its own allocator. 'self' == 'other' is a no-op
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the two sit on different allocators and the block cannot be taken,
///         leaving both as they were
/// @bigo{1} on one allocator, n on two — the block belongs to the allocator that made it
[[nodiscard]] TDA_API
tda_Status tda_arr_move_assign(tda_Arr *self, tda_Arr *other);

/// @}

/// @name compare
/// @{

/// whether the two hold the same elems, byte for byte
/// @param a one arr
/// @param b must have the same elem_size — a mismatch there is a programmer error, not a
///          false; a differing length is just false
/// @return whether the lengths match and the bytes do; being memcmp, it parts -0.0 from
///         +0.0 and counts a struct's padding
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_arr_eq(const tda_Arr *a, const tda_Arr *b);

/// whether the two hold equal elems under 'eq'
/// @param a one arr
/// @param b must have the same elem_size as 'a'
/// @param eq asked of every pair until one says no
/// @return whether the lengths match and every pair does
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_arr_eq_by(const tda_Arr *a, const tda_Arr *b, tda_Eq eq);

/// @}

/// @name info
/// @{

/// how many elems the arr holds — moved only by tda_arr_copy_assign and tda_arr_swap
/// @param self the arr
/// @return the length
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_arr_len(const tda_Arr *self);

/// the size of one elem, as named at construction
/// @param self the arr
/// @return elem_size
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_arr_elem_size(const tda_Arr *self);

/// the size of the block the arr owns
/// @param self the arr
/// @return len * elem_size
/// @bigo{1}
[[nodiscard]] TDA_API
size_t tda_arr_bytes(const tda_Arr *self);

/// the allocator the arr was built with
/// @param self the arr
/// @return the allocator, borrowed
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_arr_al(const tda_Arr *self);

/// @}

/// @name access
/// @{

/// the front elem
/// @param self asserts the arr is not empty
/// @return a pointer into the block, good until the arr is dropped, swapped or
///         copy-assigned into
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_arr_front(const tda_Arr *self);

/// the front elem, to write through
/// @copydetails tda_arr_front
[[nodiscard]] TDA_API
void *tda_arr_front_mut(tda_Arr *self);

/// the back elem
/// @copydetails tda_arr_front
[[nodiscard]] TDA_API
const void *tda_arr_back(const tda_Arr *self);

/// the back elem, to write through
/// @copydetails tda_arr_front
[[nodiscard]] TDA_API
void *tda_arr_back_mut(tda_Arr *self);

/// the elem at 'idx'
/// @param self the arr
/// @param idx asserts idx < len — out of range is a programmer error, not a status
/// @return a pointer into the block, good until the arr is dropped, swapped or
///         copy-assigned into
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_arr_get(const tda_Arr *self, size_t idx);

/// the elem at 'idx', to write through
/// @copydetails tda_arr_get
[[nodiscard]] TDA_API
void *tda_arr_get_mut(tda_Arr *self, size_t idx);

/// overwrites the elem at 'idx' with a copy of 'val'
/// @param self the arr
/// @param idx asserts idx < len
/// @param val the elem to copy in
/// @bigo{1}
TDA_API
void tda_arr_set(tda_Arr *self, size_t idx, const void *val);

/// the block itself
/// @param self the arr
/// @return the block, or null while the arr is empty; good until the arr is dropped,
///         swapped or copy-assigned into
/// @bigo{1}
[[nodiscard]] TDA_API
const void *tda_arr_data(const tda_Arr *self);

/// the block itself, to write through
/// @copydetails tda_arr_data
[[nodiscard]] TDA_API
void *tda_arr_data_mut(tda_Arr *self);

/// @}

/// @name mods
/// @{

/// exchanges the two arrs whole, lengths and all
/// @param[in,out] self one arr
/// @param[in,out] other must have the same elem_size and the same allocator: the blocks
///                      change hands where they lie, so nothing is copied and nothing can
///                      fail. 'self' == 'other' is a no-op
/// @note two allocators are a broken precondition, not a runtime state: a block belongs to
///       the one that made it. Across two: tda_arr_copy_with, then tda_arr_move_assign
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

/// a writable view of the elems, the way in to algo
/// @param self the arr
/// @return a view good until the arr is dropped, swapped or copy-assigned into
/// @bigo{1}
[[nodiscard]] TDA_API
tda_SpanMut tda_arr_to_span_mut(tda_Arr *self);

/// a read-only view of the elems
/// @copydetails tda_arr_to_span_mut
[[nodiscard]] TDA_API
tda_Span tda_arr_to_span(const tda_Arr *self);

/// @}

/// @name print
/// @{

/// writes the elems to a stream as [a, b, c], followed by a newline
/// @param self the arr
/// @param stream where to write
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_arr_fprint(const tda_Arr *self, FILE *stream, tda_FPrint fprint);

/// tda_arr_fprint to stdout
/// @param self the arr
/// @param fprint the printer, called once per elem
/// @bigo{n}
TDA_API
void tda_arr_print(const tda_Arr *self, tda_FPrint fprint);

/// @}

/// @name macros
/// @{

/// tda_arr_new_len with sizeof(T) for the elem size
/// @param T the elem type
/// @param len how many elems
/// @param al the allocator
/// @param[out] out where the new arr is written
/// @bigo{n}
#define TDA_ARR_NEW_LEN(T, len, al, out) \
    tda_arr_new_len((len), sizeof(T), (al), (out))

/// tda_arr_from_data with sizeof(T)
/// @param T the elem type
/// @param data the elems to copy in, made to typecheck as a const T *
/// @param len how many elems to read from 'data'
/// @param al the allocator
/// @param[out] out where the new arr is written
/// @bigo{n}
#define TDA_ARR_FROM_DATA(T, data, len, al, out) \
    tda_arr_from_data((const T *){ (data) }, (len), sizeof(T), (al), (out))

/// a new arr from the elems written out: TDA_ARR_OF(int32_t, al, &a, 5, 3, 1)
/// @param T the elem type
/// @param al the allocator
/// @param[out] out where the new arr is written
/// @param ... the elems, as a T initializer list
/// @bigo{n}
#define TDA_ARR_OF(T, al, out, ...)                     \
    tda_arr_from_data(                                  \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        sizeof(T), (al), (out))

/// tda_arr_front as a const T *
/// @param T the elem type
/// @param self the arr
/// @bigo{1}
#define TDA_ARR_FRONT_AS(T, self) \
    ((const T *) tda_arr_front((self)))

/// tda_arr_front_mut as a T *
/// @copydetails TDA_ARR_FRONT_AS
#define TDA_ARR_FRONT_MUT_AS(T, self) \
    ((T *) tda_arr_front_mut((self)))

/// tda_arr_back as a const T *
/// @copydetails TDA_ARR_FRONT_AS
#define TDA_ARR_BACK_AS(T, self) \
    ((const T *) tda_arr_back((self)))

/// tda_arr_back_mut as a T *
/// @copydetails TDA_ARR_FRONT_AS
#define TDA_ARR_BACK_MUT_AS(T, self) \
    ((T *) tda_arr_back_mut((self)))

/// tda_arr_get as a const T *
/// @param T the elem type
/// @param self the arr
/// @param idx the index
/// @bigo{1}
#define TDA_ARR_GET_AS(T, self, idx) \
    ((const T *) tda_arr_get((self), (idx)))

/// tda_arr_get_mut as a T *
/// @copydetails TDA_ARR_GET_AS
#define TDA_ARR_GET_MUT_AS(T, self, idx) \
    ((T *) tda_arr_get_mut((self), (idx)))

/// tda_arr_set from a value rather than an address
/// @param T the elem type; a scalar, since 'val' becomes a compound literal
/// @param self the arr
/// @param idx the index
/// @param val the value to copy in
/// @bigo{1}
#define TDA_ARR_SET(T, self, idx, val) \
    tda_arr_set((self), (idx), &(T){ (val) })

/// @}

/// @}
