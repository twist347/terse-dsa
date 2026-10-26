#pragma once

#include "tda/core/export.h"
#include "tda/core/span.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>

/// @file

/// @defgroup core_print core/print
/// @ingroup core
/// @brief tda_FPrint — how a container prints one elem
///
/// A printer writes one elem, passed by address, and nothing around it: brackets and
/// commas are the container's job. A container's fprint writes its brackets and no
/// newline, so it can be the printer of an elem that is itself a container; its print,
/// to stdout, ends the line. Every one returns negative on a stream error and
/// non-negative otherwise, as stdio does; a container's goes on writing past an error
/// and returns the first.
///
/// The types are those of core/cmp. A printer cares about width and signedness, not type
/// identity, so long long goes through tda_fprint_i64, while char, size and ptrdiff get
/// their own entries because their widths are not fixed. bool exists only here, printed
/// as a word rather than a digit.
///
/// The formats are for a human, not for a parser reading them back.
///
/// @par Example
/// @snippet core/example_print.c custom
/// @snippet core/example_print.c ctx
/// @snippet core/example_print.c ready
/// @snippet core/example_print.c use
/// @{

/// Prints one elem, passed by address, given the caller's 'ctx': a format picked at run
/// time, a table of names for an enum, the printer of a container's own elems. A
/// container calls it once per elem. Returns negative on a stream error, as fprintf does.
typedef int (*tda_FPrint)(FILE *stream, const void *val, void *ctx);

/// @name int
/// @{

/// an int8_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_i8(FILE *stream, const void *val, void *ctx);

/// an int16_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_i16(FILE *stream, const void *val, void *ctx);

/// an int32_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_i32(FILE *stream, const void *val, void *ctx);

/// an int64_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_i64(FILE *stream, const void *val, void *ctx);

/// @}

/// @name uint
/// @{

/// a uint8_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_u8(FILE *stream, const void *val, void *ctx);

/// a uint16_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_u16(FILE *stream, const void *val, void *ctx);

/// a uint32_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_u32(FILE *stream, const void *val, void *ctx);

/// a uint64_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_u64(FILE *stream, const void *val, void *ctx);

/// @}

/// @name target width
/// @{

/// a size_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_size(FILE *stream, const void *val, void *ctx);

/// a ptrdiff_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_ptrdiff(FILE *stream, const void *val, void *ctx);

/// @}

/// @name float
/// @{

/// a float through %g: readable, deliberately not round-trip. The specials print the
/// same on every platform: inf, -inf, and nan whatever its sign bit
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_f32(FILE *stream, const void *val, void *ctx);

/// a double through %g, as tda_fprint_f32
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_f64(FILE *stream, const void *val, void *ctx);

/// @}

/// @name bool
/// @{

/// true or false, never 1 and 0
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_bool(FILE *stream, const void *val, void *ctx);

/// @}

/// @name char
/// @{

/// the character if printable, else \\xNN, so nothing unprintable reaches the stream.
/// Not quoted: a ',' or ' ' elem prints as itself and reads as a separator
/// @param stream where to write
/// @param val the address of the elem
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{1}
TDA_API
int tda_fprint_char(FILE *stream, const void *val, void *ctx);

/// @}

/// @name cstr
/// @{

/// the string, quoted so one holding a comma does not read as two elems; a null pointer
/// prints as an unquoted null, distinct from "". Nothing inside is escaped, so a string
/// holding a quote still breaks the reading
/// @param stream where to write
/// @param val the address of the elem; a pointer to the const char *, not the string
/// @param ctx unused
/// @return negative on a stream error, as fprintf
/// @bigo{n}; n is the length of the string
TDA_API
int tda_fprint_cstr(FILE *stream, const void *val, void *ctx);

/// @}

/// @name span
/// @{

/// @cond
// the result of a run of writes: the first negative one, or 0 when every write went through
[[nodiscard]]
static inline int tda_fprint_join_(int acc, int rc) {
    return acc < 0 ? acc : rc < 0 ? rc : 0;
}
/// @endcond

/// writes the elems to a stream as [a, b, c], with no newline
/// @param self the view
/// @param stream where to write
/// @param fprint the printer, one call per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
static inline int tda_span_fprint(tda_Span self, FILE *stream, tda_FPrint fprint, void *ctx) {
    TDA_SPAN_ASSERT(self);
    assert(stream);
    assert(fprint);

    int rc = fputc('[', stream);
    for (size_t i = 0; i < self.len; ++i) {
        if (i > 0) {
            rc = tda_fprint_join_(rc, fputs(", ", stream));
        }
        rc = tda_fprint_join_(rc, fprint(stream, tda_span_get(self, i), ctx));
    }

    return tda_fprint_join_(rc, fputc(']', stream));
}

/// tda_span_fprint, over a writable view
/// @copydetails tda_span_fprint
static inline int tda_span_mut_fprint(tda_SpanMut self, FILE *stream, tda_FPrint fprint, void *ctx) {
    TDA_SPAN_ASSERT(self);
    assert(stream);
    assert(fprint);

    return tda_span_fprint(tda_span_mut_to_span(self), stream, fprint, ctx);
}

/// tda_span_fprint to stdout, and a newline
/// @param self the view
/// @param fprint the printer, one call per elem
/// @param ctx passed to every call of 'fprint'; may be null
/// @return negative if a write failed, as fprintf; 0 otherwise
/// @bigo{n}
static inline int tda_span_print(tda_Span self, tda_FPrint fprint, void *ctx) {
    TDA_SPAN_ASSERT(self);
    assert(fprint);

    return tda_fprint_join_(tda_span_fprint(self, stdout, fprint, ctx), fputc('\n', stdout));
}

/// tda_span_print, over a writable view
/// @copydetails tda_span_print
static inline int tda_span_mut_print(tda_SpanMut self, tda_FPrint fprint, void *ctx) {
    TDA_SPAN_ASSERT(self);
    assert(fprint);

    return tda_span_print(tda_span_mut_to_span(self), fprint, ctx);
}

/// @}

/// @}
