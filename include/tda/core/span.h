#pragma once

#include "tda/core/check.h"
#include "tda/core/print.h"
#include "tda/core/util.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

/// @file

/// @defgroup core_span core/span
/// @ingroup core
/// @brief tda_Span and tda_SpanMut — non-owning views over contiguous elems
///
/// Three fields — where the elems are, how many, how big one is — passed by value. A view
/// borrows: it never allocates, never frees, never extends the life of what it points at.
///
/// The struct is transparent, so 's.len' is a field and a compound literal is already a
/// view. There are two of them because a const struct would not make its pointee const:
/// tda_Span reads, tda_SpanMut writes, tda_span_mut_to_span converts one way only.
///
/// The seam between ds and algo. Nothing here allocates, so nothing returns a tda_Status,
/// and an index out of range asserts. A view must not outlive what it views, nor survive
/// an op that moves the source block — nothing checks that.
///
/// The typed macros write 'const T', so an elem type already spelled with const needs a
/// typedef of its own.
///
/// @par Example
/// @snippet core/example_span.c build
/// @snippet core/example_span.c access
/// @snippet core/example_span.c sub
/// @snippet core/example_span.c bridge
/// @{

/// A read-only view over contiguous elems.
typedef struct {
    const void *data; ///< the first elem; null only while 'len' is 0
    size_t len;       ///< how many elems the view spans
    size_t elem_size; ///< the size of one elem in bytes, greater than 0
} tda_Span;

/// A writable view over contiguous elems: tda_Span with a 'data' to write through.
typedef struct {
    void *data;       ///< the first elem; null only while 'len' is 0
    size_t len;       ///< how many elems the view spans
    size_t elem_size; ///< the size of one elem in bytes, greater than 0
} tda_SpanMut;

/// @name invariant
/// @{

/// asserts what every view holds: 'data' set unless empty, 'elem_size' over 0
/// @param s the view, of either type
#define TDA_SPAN_ASSERT(s)             \
    (assert((s).data || (s).len == 0), \
     assert((s).elem_size > 0))

/// @}

/// @name construction
/// @{

/// a read-only view over 'len' elems at 'data'
/// @param data the first elem; null only when 'len' is 0
/// @param len elems in the view
/// @param elem_size bytes in one elem, greater than 0
/// @return the view
/// @bigo{1}
[[nodiscard]]
static inline tda_Span tda_span_from_data(const void *data, size_t len, size_t elem_size) {
    assert(data || len == 0);
    assert(elem_size > 0);

    return (tda_Span){
        .data = data,
        .len = len,
        .elem_size = elem_size
    };
}

/// tda_span_from_data, writable
/// @param data the first elem; null only when 'len' is 0
/// @param len elems in the view
/// @param elem_size bytes in one elem, greater than 0
/// @return the view
/// @bigo{1}
[[nodiscard]]
static inline tda_SpanMut tda_span_from_data_mut(void *data, size_t len, size_t elem_size) {
    assert(data || len == 0);
    assert(elem_size > 0);

    return (tda_SpanMut){
        .data = data,
        .len = len,
        .elem_size = elem_size
    };
}

/// @}

/// @name to span
/// @{

/// the same elems, seen read-only
/// @param s the view to give up the write rights of
/// @return the read-only view; there is no way back
/// @bigo{1}
[[nodiscard]]
static inline tda_Span tda_span_mut_to_span(tda_SpanMut s) {
    TDA_SPAN_ASSERT(s);

    return (tda_Span){
        .data = s.data,
        .len = s.len,
        .elem_size = s.elem_size
    };
}

/// @}

/// @name subspan
/// @{

/// a view over 'count' elems of 'self', starting at 'idx'
/// @param self the view to narrow
/// @param idx where it begins; asserts idx <= self.len, == gives an empty view
/// @param count elems it spans; asserts count <= self.len - idx
/// @return the narrower view, over the same memory — nothing is copied
/// @bigo{1}
[[nodiscard]]
static inline tda_Span tda_span_sub(tda_Span self, size_t idx, size_t count) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(idx <= self.len);
    TDA_EXPECT(count <= self.len - idx);

    return (tda_Span){
        .data = self.data ? (const unsigned char *) self.data + idx * self.elem_size : nullptr,
        .len = count,
        .elem_size = self.elem_size
    };
}

/// tda_span_sub, writable
/// @param self the view to narrow
/// @param idx where it begins; asserts idx <= self.len, == gives an empty view
/// @param count elems it spans; asserts count <= self.len - idx
/// @return the narrower view, over the same memory — nothing is copied
/// @bigo{1}
[[nodiscard]]
static inline tda_SpanMut tda_span_sub_mut(tda_SpanMut self, size_t idx, size_t count) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(idx <= self.len);
    TDA_EXPECT(count <= self.len - idx);

    return (tda_SpanMut){
        .data = self.data ? (unsigned char *) self.data + idx * self.elem_size : nullptr,
        .len = count,
        .elem_size = self.elem_size
    };
}

/// @}

/// @name info
/// @{

/// the size of what the view spans; a writable view converts first
/// @param self the view
/// @return len * elem_size
/// @bigo{1}
[[nodiscard]]
static inline size_t tda_span_bytes(tda_Span self) {
    TDA_SPAN_ASSERT(self);

    return self.len * self.elem_size;
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

    return (const unsigned char *) self.data + idx * self.elem_size;
}

/// tda_span_get, to write through
/// @param self the view
/// @param idx the index; asserts idx < self.len
/// @return a pointer to the elem
/// @bigo{1}
[[nodiscard]]
static inline void *tda_span_get_mut(tda_SpanMut self, size_t idx) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(idx < self.len);

    return (unsigned char *) self.data + idx * self.elem_size;
}

/// writes one elem over the elem at 'idx', in the borrowed memory itself
/// @param self the view
/// @param idx the index; asserts idx < self.len
/// @param val the address of the value; elem_size bytes are read from it
/// @bigo{1}
static inline void tda_span_set(tda_SpanMut self, size_t idx, const void *val) {
    assert(val);

    memcpy(tda_span_get_mut(self, idx), val, self.elem_size);
}

/// @}

/// @name mods
/// @{

/// exchanges two elems in place
/// @param self the view
/// @param i one index; asserts it is in range
/// @param j the other index; asserts it is in range; i == j is a no-op
/// @bigo{1}
static inline void tda_span_swap_elems(tda_SpanMut self, size_t i, size_t j) {
    TDA_SPAN_ASSERT(self);
    TDA_EXPECT(i < self.len);
    TDA_EXPECT(j < self.len);

    if (i == j) {
        return;
    }

    unsigned char *a = tda_span_get_mut(self, i);
    unsigned char *b = tda_span_get_mut(self, j);
    for (size_t k = 0; k < self.elem_size; ++k) {
        TDA_SWAP(a[k], b[k]);
    }
}

/// @}

/// @name print
/// @{

/// writes the elems to a stream as [a, b, c], followed by a newline
/// @param self the view
/// @param stream where to write
/// @param fprint the printer, one call per elem
/// @bigo{n}
static inline void tda_span_fprint(tda_Span self, FILE *stream, tda_FPrint fprint) {
    TDA_SPAN_ASSERT(self);
    assert(stream);
    assert(fprint);

    fputc('[', stream);
    for (size_t i = 0; i < self.len; ++i) {
        if (i > 0) {
            fputs(", ", stream);
        }
        fprint(stream, tda_span_get(self, i));
    }
    fputs("]\n", stream);
}

/// tda_span_fprint, over a writable view
/// @param self the view
/// @param stream where to write
/// @param fprint the printer, one call per elem
/// @bigo{n}
static inline void tda_span_mut_fprint(tda_SpanMut self, FILE *stream, tda_FPrint fprint) {
    TDA_SPAN_ASSERT(self);
    assert(stream);
    assert(fprint);

    tda_span_fprint(tda_span_mut_to_span(self), stream, fprint);
}

/// tda_span_fprint to stdout: [a, b, c] and a newline
/// @param self the view
/// @param fprint the printer, one call per elem
/// @bigo{n}
static inline void tda_span_print(tda_Span self, tda_FPrint fprint) {
    TDA_SPAN_ASSERT(self);
    assert(fprint);

    tda_span_fprint(self, stdout, fprint);
}

/// tda_span_print, over a writable view
/// @param self the view
/// @param fprint the printer, one call per elem
/// @bigo{n}
static inline void tda_span_mut_print(tda_SpanMut self, tda_FPrint fprint) {
    TDA_SPAN_ASSERT(self);
    assert(fprint);

    tda_span_fprint(tda_span_mut_to_span(self), stdout, fprint);
}

/// @}

/// @name macros
/// @{

/// tda_span_from_data with sizeof(T) for the elem size
/// @param T the elem type
/// @param data the first elem, made to typecheck as a const T *
/// @param len elems in the view
/// @bigo{1}
#define TDA_SPAN_FROM_DATA(T, data, len) \
    tda_span_from_data((const T *){ (data) }, (len), sizeof(T))

/// tda_span_from_data_mut with sizeof(T)
/// @param T the elem type
/// @param data the first elem, made to typecheck as a T *
/// @param len elems in the view
/// @bigo{1}
#define TDA_SPAN_FROM_DATA_MUT(T, data, len) \
    tda_span_from_data_mut((T *){ (data) }, (len), sizeof(T))

/// a view over the elems written out: TDA_SPAN_OF(int32_t, 5, 3, 1)
/// @param T the elem type
/// @param ... the elems, as a T initializer list
/// @warning the elems are a compound literal, gone at the end of the enclosing block
/// @bigo{1}
#define TDA_SPAN_OF(T, ...)                             \
    tda_span_from_data(                                 \
        (const T[]){ __VA_ARGS__ },                     \
        sizeof((const T[]){ __VA_ARGS__ }) / sizeof(T), \
        sizeof(T))

/// TDA_SPAN_OF, writable
/// @param T the elem type
/// @param ... the elems, as a T initializer list
/// @warning the elems are a compound literal, gone at the end of the enclosing block
/// @bigo{1}
#define TDA_SPAN_OF_MUT(T, ...)                   \
    tda_span_from_data_mut(                       \
        (T[]){ __VA_ARGS__ },                     \
        sizeof((T[]){ __VA_ARGS__ }) / sizeof(T), \
        sizeof(T))

/// tda_span_get as a const T *
/// @param T the elem type
/// @param s the view
/// @param idx the index
/// @bigo{1}
#define TDA_SPAN_GET_AS(T, s, idx) \
    ((const T *) tda_span_get((s), (idx)))

/// tda_span_get_mut as a T *
/// @param T the elem type
/// @param s the view
/// @param idx the index
/// @bigo{1}
#define TDA_SPAN_GET_MUT_AS(T, s, idx) \
    ((T *) tda_span_get_mut((s), (idx)))

/// walks the span front to back, binding 'elem' to each elem in turn
/// @param T the elem type: a span carries sizes, not types, so the walk has to be told
/// @param elem the name the loop variable takes; it is a const T *
/// @param s the view, evaluated once — tda_vec_to_span(v) and its kin belong here
/// @bigo{n} over the whole walk
#define TDA_SPAN_FOR_EACH_AS(T, elem, s)                                \
    for (tda_Span span_ = (s), *once_ = &span_; once_; once_ = nullptr) \
        for (const T *elem = span_.data, *end_ = elem + span_.len;      \
             elem != end_;                                              \
             ++elem)

/// the same walk over elems that may be written through
/// @copydetails TDA_SPAN_FOR_EACH_AS
#define TDA_SPAN_FOR_EACH_MUT_AS(T, elem, s)                               \
    for (tda_SpanMut span_ = (s), *once_ = &span_; once_; once_ = nullptr) \
        for (T *elem = span_.data, *end_ = elem + span_.len;               \
             elem != end_;                                                 \
             ++elem)

/// tda_span_set from a value rather than its address
/// @param T the elem type — scalars only, (T){ val } takes nothing else
/// @param s the view
/// @param idx the index
/// @param val the value to copy in
/// @bigo{1}
#define TDA_SPAN_SET(T, s, idx, val) \
    tda_span_set((s), (idx), &(T){ (val) })

/// @}

/// @}
