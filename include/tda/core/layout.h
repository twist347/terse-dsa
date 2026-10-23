#pragma once

#include <assert.h>
#include <stdckdint.h>
#include <stddef.h>

/// @file

/// @defgroup core_layout core/layout
/// @ingroup core
/// @brief tda_Layout — the size and the alignment of a block, as one value
///
/// What an allocator is asked for and what it is given back: Rust's Layout. Two size_t
/// side by side swap without a word from the compiler; one struct does not.
///
/// The alignment is a power of two. The size may be anything, 0 included, and need not
/// be a multiple of the alignment: a request for 1 byte at 64 is a fine one. A layout
/// that describes a type, as TDA_LAYOUT_OF gives, has a size that is a multiple of its
/// alignment, and only such a layout can be repeated into an array.
///
/// The arithmetic that can overflow is checked, in the shape of C23's ckd_add: true
/// means it overflowed and '*out' is unspecified.
///
/// Header-only: every function is static inline.
///
/// @par Example
/// @snippet core/example_layout.c type_def
/// @snippet core/example_layout.c type
/// @snippet core/example_layout.c build
/// @{

/// The size and the alignment of a block.
typedef struct {
    size_t size;  ///< in bytes
    size_t align; ///< a power of two
} tda_Layout;

/// the layout of one T
/// @param T the type
#define TDA_LAYOUT_OF(T) ((tda_Layout){.size = sizeof(T), .align = alignof(T)})

/// @name info
/// @{

/// whether 'align' is a power of two, as every alignment must be
/// @param align the value to check
/// @return whether it is greater than 0 with a single bit set
/// @bigo{1}
[[nodiscard]]
static inline bool tda_layout_is_align(size_t align) {
    return align > 0 && (align & (align - 1)) == 0;
}

/// whether the layout could describe a type: a power-of-two alignment and a non-zero
/// size that is a multiple of it
/// @param layout the layout
/// @return what TDA_LAYOUT_OF always gives
/// @bigo{1}
[[nodiscard]]
static inline bool tda_layout_is_type(tda_Layout layout) {
    return tda_layout_is_align(layout.align) && layout.size > 0 && layout.size % layout.align == 0;
}

/// whether the two describe the same block: equal sizes and equal alignments
/// @param a one layout
/// @param b the other
/// @return whether both fields match
/// @bigo{1}
[[nodiscard]]
static inline bool tda_layout_eq(tda_Layout a, tda_Layout b) {
    return a.size == b.size && a.align == b.align;
}

/// @}

/// @name build
/// @{

/// the layout of 'count' elems of 'elem', back to back
/// @param[out] out the array's layout: count * elem.size bytes at elem.align
/// @param elem one elem; asserted to be a type layout, so the elems need no padding
/// @param count how many
/// @return true when the size overflows size_t
/// @bigo{1}
[[nodiscard]]
static inline bool tda_layout_ckd_array(tda_Layout *out, tda_Layout elem, size_t count) {
    assert(out);
    assert(tda_layout_is_type(elem));

    out->align = elem.align;

    return ckd_mul(&out->size, elem.size, count);
}

/// 'self' with 'field' put after it, as the next member of a struct: at the first offset
/// past 'self' aligned for 'field'
/// @param[in,out] self the layout so far, which grows to take the field and to the larger
///                     of the two alignments
/// @param field what comes next
/// @param[out] out_offset where the field starts
/// @return true when the size overflows size_t
/// @note the size is not padded at the end; tda_layout_ckd_pad does that once the last
///       field is in
/// @bigo{1}
[[nodiscard]]
static inline bool tda_layout_ckd_extend(tda_Layout *self, tda_Layout field, size_t *out_offset) {
    assert(self);
    assert(out_offset);
    assert(tda_layout_is_align(self->align));
    assert(tda_layout_is_align(field.align));

    // the padding is what lifts the size to the next multiple of the field's alignment
    const size_t pad = -self->size & (field.align - 1);
    size_t offset;
    if (ckd_add(&offset, self->size, pad) || ckd_add(&self->size, offset, field.size)) {
        return true;
    }

    *out_offset = offset;
    if (field.align > self->align) {
        self->align = field.align;
    }

    return false;
}

/// 'self' with its size rounded up to its alignment, so copies of it tile as an array or
/// a struct's last member does
/// @param[in,out] self the layout
/// @return true when the size overflows size_t
/// @bigo{1}
[[nodiscard]]
static inline bool tda_layout_ckd_pad(tda_Layout *self) {
    assert(self);
    assert(tda_layout_is_align(self->align));

    return ckd_add(&self->size, self->size, -self->size & (self->align - 1));
}

/// @}

/// @}
