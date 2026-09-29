#pragma once

#include "tda/core/export.h"

#include <stdio.h>

/// @file

/// @defgroup core_print core/print
/// @ingroup core
/// @brief tda_FPrint — how a container is told to show one elem
///
/// A printer writes one elem, handed by address, and nothing around it: the brackets and
/// the commas belong to the container printing through it.
///
/// The type set is core/cmp's — a printer sees a width and a signedness, not a type
/// identity, so long long is served by tda_fprint_i64, while char, size and ptrdiff have
/// entries of their own because their widths are not fixed. bool is the one entry only
/// here, reading as a word rather than a digit.
///
/// The formats are for a human, not for a parser reading them back.
///
/// @par Example
/// @snippet core/example_print.c custom
/// @snippet core/example_print.c ready
/// @snippet core/example_print.c use
/// @{

/// The printer a container is handed to show itself: one call per elem, the elem by
/// address.
typedef void (*tda_FPrint)(FILE *, const void *);

/// @name int
/// @{

/// an int8_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_i8(FILE *stream, const void *val);

/// an int16_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_i16(FILE *stream, const void *val);

/// an int32_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_i32(FILE *stream, const void *val);

/// an int64_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_i64(FILE *stream, const void *val);

/// @}

/// @name uint
/// @{

/// a uint8_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_u8(FILE *stream, const void *val);

/// a uint16_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_u16(FILE *stream, const void *val);

/// a uint32_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_u32(FILE *stream, const void *val);

/// a uint64_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_u64(FILE *stream, const void *val);

/// @}

/// @name target width
/// @{

/// a size_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_size(FILE *stream, const void *val);

/// a ptrdiff_t, in decimal
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_ptrdiff(FILE *stream, const void *val);

/// @}

/// @name float
/// @{

/// a float through %g — readable, and deliberately not round-trip. The specials are
/// spelled the same everywhere: inf, -inf, and nan whatever its sign bit
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_f32(FILE *stream, const void *val);

/// a double through %g, as tda_fprint_f32
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_f64(FILE *stream, const void *val);

/// @}

/// @name bool
/// @{

/// true or false, never 1 and 0
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_bool(FILE *stream, const void *val);

/// @}

/// @name char
/// @{

/// the character when printable, \\xNN when not, so an elem is always one token
/// @param stream where to write
/// @param val the address of the elem
/// @bigo{1}
TDA_API
void tda_fprint_char(FILE *stream, const void *val);

/// @}

/// @name cstr
/// @{

/// the string, quoted, so one holding a comma cannot read as two elems; a null pointer
/// prints as an unquoted null, a value of its own rather than ""
/// @param stream where to write
/// @param val the address of the elem — a pointer to the const char *, not the string
/// @bigo{n} — n is the length of the string
TDA_API
void tda_fprint_cstr(FILE *stream, const void *val);

/// @}

/// @}
