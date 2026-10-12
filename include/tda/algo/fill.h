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
/// @param val points to elem_size bytes, read once per elem
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

/// @}
