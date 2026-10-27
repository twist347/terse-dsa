#pragma once

#include "tda/core/check.h"
#include "tda/core/layout.h"
#include "tda/core/util.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/// @file

/// @defgroup core_span core/span
/// @ingroup core
/// @brief tda_Span and tda_SpanMut — non-owning views over contiguous elems
///
/// Three fields (where the elems are, how many, and the layout of one), passed by value. A
/// view borrows: it never allocates, frees or extends the life of what it points at. The
/// layout is the elem's whole one, alignment included, so an algo that needs room for
/// copies of the elems asks for exactly what they need.
///
/// The struct is transparent, so 's.len' is a field and a compound literal is already a
/// view. There are two types because a const struct would not make its pointee const:
/// tda_Span reads, tda_SpanMut writes, and tda_span_mut_to_span converts one way only.
///
/// Two spans meet only on one layout: an algo over two of them, or over a span and a
/// container, asserts the layouts are equal, alignment included. Only algo/transform maps
/// between layouts.
///
/// Spans are the seam between ds and algo. Nothing here allocates, so nothing returns a
/// tda_Status, and out-of-range indices assert. A view must not outlive what it views,
/// nor survive an op that moves the source block; nothing checks that.
///
/// The typed macros write 'const typeof(T)', so T may be any type name, a pointer type
/// such as 'const char *' or one already const included.
///
/// @par Example
/// @snippet core/example_span.c build
/// @snippet core/example_span.c access
/// @snippet core/example_span.c sub
/// @snippet core/example_span.c overlap
/// @snippet core/example_span.c bridge
/// @{

/// A read-only view over contiguous elems.
typedef struct {
    const void *data; ///< the first elem; null only while 'len' is 0
    size_t len;       ///< how many elems the view spans
    tda_Layout elem;  ///< the layout of one elem, a type's: TDA_LAYOUT_OF gives it
} tda_Span;

/// A writable view over contiguous elems: tda_Span with a 'data' to write through.
typedef struct {
    void *data;      ///< the first elem; null only while 'len' is 0
    size_t len;      ///< how many elems the view spans
    tda_Layout elem; ///< the layout of one elem, a type's: TDA_LAYOUT_OF gives it
} tda_SpanMut;

/// @name invariant
/// @{

/// asserts what every view holds: 'data' set unless empty and on the elem's alignment,
/// 'elem' a type's layout
/// @param s the view, of either type; evaluated more than once, so pass a name, not a call
///          or an expression with a side effect. Under NDEBUG it is not evaluated at all
#define TDA_SPAN_ASSERT(s)                 \
    (assert((s).data || (s).len == 0),     \
     assert(tda_layout_is_type((s).elem)), \
     assert(((uintptr_t) (s).data & ((s).elem.align - 1)) == 0))

/// @}

/// @name construction
/// @{

/// a read-only view over 'len' elems at 'data'
/// @param data the first elem, on elem.align; null only when 'len' is 0
/// @param len elems in the view
/// @param elem the layout of one elem, asserted to be a type's
/// @return the view
/// @bigo{1}
[[nodiscard]]
static inline tda_Span tda_span_from_data(const void *data, size_t len, tda_Layout elem) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(((uintptr_t) data & (elem.align - 1)) == 0); // a misaligned T would be read through

    return (tda_Span){
        .data = data,
        .len = len,
        .elem = elem
    };
}

/// tda_span_from_data, writable
/// @param data the first elem, on elem.align; null only when 'len' is 0
/// @param len elems in the view
/// @param elem the layout of one elem, asserted to be a type's
/// @return the view
/// @bigo{1}
[[nodiscard]]
static inline tda_SpanMut tda_span_from_data_mut(void *data, size_t len, tda_Layout elem) {
    assert(data || len == 0);
    assert(tda_layout_is_type(elem));
    assert(((uintptr_t) data & (elem.align - 1)) == 0); // a misaligned T would be read through

    return (tda_SpanMut){
        .data = data,
        .len = len,
        .elem = elem
    };
}

/// @}

/// @name to span
/// @{

/// the same elems, seen read-only
/// @param s the view to make read-only
/// @return the read-only view; there is no way back
/// @bigo{1}
[[nodiscard]]
static inline tda_Span tda_span_mut_to_span(tda_SpanMut s) {
    TDA_SPAN_ASSERT(s);

    return (tda_Span){
        .data = s.data,
        .len = s.len,
        .elem = s.elem
    };
}

/// @}

/// @name subspan
/// @{

/// a view over 'count' elems of 'self', starting at 'idx'
/// @param self the view to narrow
/// @param idx the start; asserts idx <= self.len, and idx == len gives an empty view
/// @param count elems it spans; asserts count <= self.len - idx
/// @return the narrower view over the same memory; nothing is copied
/// @bigo{1}
[[nodiscard]]
static inline tda_Span tda_span_sub(tda_Span self, size_t idx, size_t count) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(idx <= self.len);
    TDA_EXPECT(count <= self.len - idx);

    return (tda_Span){
        .data = self.data ? (const unsigned char *) self.data + idx * self.elem.size : nullptr,
        .len = count,
        .elem = self.elem
    };
}

/// tda_span_sub, writable
/// @param self the view to narrow
/// @param idx the start; asserts idx <= self.len, and idx == len gives an empty view
/// @param count elems it spans; asserts count <= self.len - idx
/// @return the narrower view over the same memory; nothing is copied
/// @bigo{1}
[[nodiscard]]
static inline tda_SpanMut tda_span_sub_mut(tda_SpanMut self, size_t idx, size_t count) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(idx <= self.len);
    TDA_EXPECT(count <= self.len - idx);

    return (tda_SpanMut){
        .data = self.data ? (unsigned char *) self.data + idx * self.elem.size : nullptr,
        .len = count,
        .elem = self.elem
    };
}

/// cuts 'self' in two at 'idx': the elems before it and the elems from it on
/// @param self the view to cut
/// @param idx where; asserts idx <= self.len, and 0 or len leaves one side empty
/// @param[out] out_head the first idx elems
/// @param[out] out_tail the other self.len - idx
/// @bigo{1}; nothing is copied
static inline void tda_span_split_at(tda_Span self, size_t idx, tda_Span *out_head, tda_Span *out_tail) {
    TDA_SPAN_ASSERT(self);
    assert(out_head);
    assert(out_tail);
    TDA_EXPECT(idx <= self.len);

    *out_head = tda_span_sub(self, 0, idx);
    *out_tail = tda_span_sub(self, idx, self.len - idx);
}

/// tda_span_split_at, writable; the two halves never overlap
/// @param self the view to cut
/// @param idx where; asserts idx <= self.len, and 0 or len leaves one side empty
/// @param[out] out_head the first idx elems
/// @param[out] out_tail the other self.len - idx
/// @bigo{1}; nothing is copied
static inline void tda_span_split_at_mut(tda_SpanMut self, size_t idx, tda_SpanMut *out_head, tda_SpanMut *out_tail) {
    TDA_SPAN_ASSERT(self);
    assert(out_head);
    assert(out_tail);
    TDA_EXPECT(idx <= self.len);

    *out_head = tda_span_sub_mut(self, 0, idx);
    *out_tail = tda_span_sub_mut(self, idx, self.len - idx);
}

/// @}

/// @name info
/// @{

/// the size of what the view spans; a writable view converts first
/// @param self the view
/// @return len * elem.size
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_span_bytes(tda_Span self) {
    TDA_SPAN_ASSERT(self);

    return self.len * self.elem.size;
}

/// @}

/// @name overlap
/// @{

/// whether two views share a byte, what an algo output that must not overlap an input
/// forbids; a writable view converts first
/// @param a one view
/// @param b the other
/// @return false when either is empty
/// @bigo{1}
[[nodiscard]]
static inline bool tda_span_overlap(tda_Span a, tda_Span b) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);

    if (a.len == 0 || b.len == 0) {
        return false;
    }

    // as integers: '<' between pointers into different objects is undefined
    const uintptr_t pa = (uintptr_t) a.data, pb = (uintptr_t) b.data;

    return pa < pb + tda_span_bytes(b) && pb < pa + tda_span_bytes(a);
}

/// whether two views are apart or the very same elems, the two cases an elementwise op
/// such as tda_span_copy takes
/// @param a one view
/// @param b the other
/// @return true when they share no byte or start at the same one
/// @bigo{1}
[[nodiscard]]
static inline bool tda_span_apart_or_same(tda_Span a, tda_Span b) {
    TDA_SPAN_ASSERT(a);
    TDA_SPAN_ASSERT(b);

    return a.data == b.data || !tda_span_overlap(a, b);
}

/// @}

/// @name access
/// @{

/// the elem at 'idx'
/// @param self the view
/// @param idx the index; asserts idx < self.len
/// @return a pointer to the elem
/// @bigo{1}
[[nodiscard]]
static inline const void *tda_span_get(tda_Span self, size_t idx) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(idx < self.len);

    return (const unsigned char *) self.data + idx * self.elem.size;
}

/// tda_span_get, writable
/// @param self the view
/// @param idx the index; asserts idx < self.len
/// @return a pointer to the elem
/// @bigo{1}
[[nodiscard]]
static inline void *tda_span_get_mut(tda_SpanMut self, size_t idx) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(idx < self.len);

    return (unsigned char *) self.data + idx * self.elem.size;
}

/// copies one elem over the elem at 'idx', in the viewed memory
/// @param self the view
/// @param idx the index; asserts idx < self.len
/// @param val the address of the value; elem.size bytes are read from it
/// @bigo{1}
static inline void tda_span_set(tda_SpanMut self, size_t idx, const void *val) {
    TDA_SPAN_ASSERT(self);
    assert(val);
    TDA_EXPECT(idx < self.len);

    // memmove: 'val' may be the very elem being set
    memmove(tda_span_get_mut(self, idx), val, self.elem.size);
}

/// @}

/// @name mods
/// @{

/// exchanges two elems in place
/// @param self the view
/// @param i one index, asserted in range
/// @param j the other index, asserted in range; may equal 'i'
/// @bigo{1}
static inline void tda_span_swap_elems(tda_SpanMut self, size_t i, size_t j) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(i < self.len);
    TDA_EXPECT(j < self.len);

    if (i == j) {
        return;
    }

    unsigned char *a = tda_span_get_mut(self, i), *b = tda_span_get_mut(self, j);
    for (size_t k = 0; k < self.elem.size; ++k) {
        TDA_SWAP(a[k], b[k]);
    }
}

/// @}

/// @name macros
/// @{

/// @cond
// return the view after checking its elem layout against TDA_LAYOUT_OF(T), so a typed
// macro evaluates it once and still checks it

[[nodiscard]]
static inline tda_Span tda_span_typed_(tda_Span s, [[maybe_unused]] tda_Layout elem) {
    TDA_SPAN_ASSERT(s);
    TDA_EXPECT(tda_layout_eq(s.elem, elem));
    return s;
}

[[nodiscard]]
static inline tda_SpanMut tda_span_mut_typed_(tda_SpanMut s, [[maybe_unused]] tda_Layout elem) {
    TDA_SPAN_ASSERT(s);
    TDA_EXPECT(tda_layout_eq(s.elem, elem));
    return s;
}
/// @endcond

/// tda_span_from_data with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param data the first elem, converted to a const T *: a pointer to another type is an
///             error, a void * passes unchecked
/// @param len elems in the view
/// @bigo{1}
#define TDA_SPAN_FROM_DATA(T, data, len) \
    tda_span_from_data((const typeof(T) *){ (data) }, (len), TDA_LAYOUT_OF(T))

/// tda_span_from_data_mut with TDA_LAYOUT_OF(T)
/// @param T the elem type
/// @param data the first elem, converted to a T *: a pointer to another type is an
///             error, a void * passes unchecked
/// @param len elems in the view
/// @bigo{1}
#define TDA_SPAN_FROM_DATA_MUT(T, data, len) \
    tda_span_from_data_mut((T *){ (data) }, (len), TDA_LAYOUT_OF(T))

/// a view over the elems written out: TDA_SPAN_OF(int32_t, 5, 3, 1)
/// @param T the elem type
/// @param ... the elems, as a T initializer list, not empty: C has no array of zero
///            length, and an empty list builds one only as a compiler extension
/// @warning the elems are a compound literal, gone at the end of the enclosing block
/// @bigo{1}
#define TDA_SPAN_OF(T, ...)                                     \
    tda_span_from_data(                                         \
        (const typeof(T)[]){ __VA_ARGS__ },                     \
        sizeof((const typeof(T)[]){ __VA_ARGS__ }) / sizeof(T), \
        TDA_LAYOUT_OF(T))

/// TDA_SPAN_OF, writable
/// @param T the elem type
/// @param ... the elems, as a T initializer list, not empty
/// @warning the elems are a compound literal, gone at the end of the enclosing block
/// @bigo{1}
#define TDA_SPAN_OF_MUT(T, ...)                   \
    tda_span_from_data_mut(                       \
        (T[]){ __VA_ARGS__ },                     \
        sizeof((T[]){ __VA_ARGS__ }) / sizeof(T), \
        TDA_LAYOUT_OF(T))

/// tda_span_get as a const T *
/// @param T the elem type
/// @param self the view
/// @param idx the index
/// @bigo{1}
#define TDA_SPAN_GET_AS(T, self, idx) \
    ((const typeof(T) *) tda_span_get(tda_span_typed_((self), TDA_LAYOUT_OF(T)), (idx)))

/// tda_span_get_mut as a T *
/// @param T the elem type
/// @param self the view
/// @param idx the index
/// @bigo{1}
#define TDA_SPAN_GET_MUT_AS(T, self, idx) \
    ((T *) tda_span_get_mut(tda_span_mut_typed_((self), TDA_LAYOUT_OF(T)), (idx)))

/// @cond
// the body of both span walks; the hidden names come in already unique. An empty view
// may carry a null data pointer, and null + 0 is undefined in C23, so the end is formed
// only when there are elems to step over
#define TDA_SPAN_WALK_(SpanT, ElemT, elem, view, span, once, end)               \
    for (SpanT span = (view), *once = &span; once; once = nullptr)              \
        for (ElemT *elem = span.data, *end = span.len ? elem + span.len : elem; \
             elem != end;                                                       \
             ++elem)
/// @endcond

/// walks the span front to back, binding 'elem' to each elem in turn
/// @param T the elem type; a span carries layouts, not types
/// @param elem the name of the loop variable, a const T *
/// @param self the view, evaluated once, so tda_vec_to_span(v) and the like are fine here
/// @bigo{n} for the whole walk
#define TDA_SPAN_FOR_EACH_AS(T, elem, self)        \
    TDA_SPAN_WALK_(                                \
        tda_Span,                                  \
        const typeof(T),                           \
        elem,                                      \
        tda_span_typed_((self), TDA_LAYOUT_OF(T)), \
        TDA_LINE_NAME_(span_),                     \
        TDA_LINE_NAME_(once_),                     \
        TDA_LINE_NAME_(end_)                       \
    )

/// the same walk over elems that may be written through
/// @copydetails TDA_SPAN_FOR_EACH_AS
#define TDA_SPAN_FOR_EACH_MUT_AS(T, elem, self)        \
    TDA_SPAN_WALK_(                                    \
        tda_SpanMut,                                   \
        T,                                             \
        elem,                                          \
        tda_span_mut_typed_((self), TDA_LAYOUT_OF(T)), \
        TDA_LINE_NAME_(span_),                         \
        TDA_LINE_NAME_(once_),                         \
        TDA_LINE_NAME_(end_)                           \
    )

/// tda_span_set from a value rather than its address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param self the view
/// @param idx the index
/// @param val the value to copy in
/// @bigo{1}
#define TDA_SPAN_SET(T, self, idx, val) \
    tda_span_set(tda_span_mut_typed_((self), TDA_LAYOUT_OF(T)), (idx), &(T[1]){ (val) }[0])

/// @}

/// @}
