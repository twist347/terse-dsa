#pragma once

#include "tda/algo/fn.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_transform algo/transform
/// @ingroup algo
/// @brief mapping a span elemwise into another
///
/// Only the lengths must match. Unlike the rest of algo, the elem layouts may differ: the
/// op knows both types and each span is walked with its own stride, so a span of Pair can
/// map into a span of int64_t. To copy bytes unchanged, use algo/copy.
///
/// The destination may be a source if its elems are no wider than the source's; wider
/// ones would overwrite elems not yet read. Partial overlap is never allowed, since each
/// position is written before the next is read. In place the op gets one address as
/// both 'dst' and an operand; see tda_UnOp and tda_BinOp.
///
/// @par Example
/// @snippet algo/example_transform.c ops
/// @snippet algo/example_transform.c map
/// @{

/// @name transform
/// @{

/// writes op(src[i]) at dst[i]
/// @param dst asserts the same len as 'src'
/// @param src the operands
/// @param op called once per position
/// @param ctx handed to 'op'
/// @bigo{n}
TDA_API
void tda_span_transform(tda_SpanMut dst, tda_Span src, tda_UnOp op, void *ctx);

/// writes op(a[i], b[i]) at dst[i]
/// @param dst asserts the same len as both sources
/// @param a one source
/// @param b the other
/// @param op called once per position
/// @param ctx handed to 'op'
/// @bigo{n}
TDA_API
void tda_span_zip_with(tda_SpanMut dst, tda_Span a, tda_Span b, tda_BinOp op, void *ctx);

/// @}

/// @}
