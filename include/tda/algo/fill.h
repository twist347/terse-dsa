#pragma once

#include "tda/algo/fn.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_fill algo/fill
/// @ingroup algo
/// @brief writing every position of a span
///
/// @par Example
/// @snippet algo/example_fill.c gen
/// @snippet algo/example_fill.c fill
/// @{

/// @name fill
/// @{

/// writes one value over every elem
/// @param s the span
/// @param val points to elem.size bytes, read once; may be one of the elems
/// @bigo{n}
TDA_API
void tda_span_fill(tda_SpanMut s, const void *val);

/// zeroes every byte of the span
/// @param s the span
/// @bigo{n}
TDA_API
void tda_span_fill_zero(tda_SpanMut s);

/// writes every position from 'gen'
/// @param s the span
/// @param gen called per position with its index; a counter in 'ctx' gives iota
/// @param ctx handed to 'gen'
/// @bigo{n}
TDA_API
void tda_span_generate(tda_SpanMut s, tda_Gen gen, void *ctx);

/// @}

/// @name macros
/// @{

/// tda_span_fill from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param s the span
/// @param val the value to write
/// @bigo{n}
#define TDA_SPAN_FILL(T, s, val) \
    tda_span_fill(tda_span_mut_typed_((s), TDA_LAYOUT_OF(T)), &(typeof(T)[1]){ (val) }[0])

/// @}

/// @}
