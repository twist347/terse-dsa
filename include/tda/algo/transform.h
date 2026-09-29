#pragma once

#include "tda/algo/fn.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_transform algo/transform
/// @ingroup algo
/// @brief mapping a span elemwise into another
///
/// Only the lengths have to match: unlike the rest of algo the elem sizes need not, since
/// the op knows both types and each span is walked with its own stride — a span of Pair
/// maps into a span of int64_t. algo/copy, which moves bytes as they are, is the other
/// half of that line.
///
/// A destination that is a source is fine while its elems are no wider than the source's
/// — a wider one writes over elems not yet read. A partial overlap is never fine, since
/// every position is written before the next is read.
///
/// @par Example
/// @snippet algo/example_transform.c ops
/// @snippet algo/example_transform.c map
/// @{

/// @name transform
/// @{

/// writes op(src[i]) at dst[i]
/// @param dst where the results go; asserts the same len as 'src'
/// @param src where the operands come from
/// @param op what to apply, called once per position
/// @param ctx handed to 'op'
/// @bigo{n}
TDA_API
void tda_span_transform(tda_SpanMut dst, tda_Span src, tda_UnOp op, void *ctx);

/// writes op(a[i], b[i]) at dst[i], the two sources walked in step
/// @param dst where the results go; asserts the same len as both sources
/// @param a one source
/// @param b the other
/// @param op what to apply, called once per position
/// @param ctx handed to 'op'
/// @bigo{n}
TDA_API
void tda_span_zip_with(tda_SpanMut dst, tda_Span a, tda_Span b, tda_BinOp op, void *ctx);

/// @}

/// @}
