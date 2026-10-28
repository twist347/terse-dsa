#pragma once

#include "tda/algo/fn.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_fold algo/fold
/// @ingroup algo
/// @brief folding a span into one value, and keeping the steps
///
/// The accumulator belongs to the caller and has the caller's type, so a span of int32_t
/// can fold into an int64_t, a count or a struct. An empty span leaves it untouched, so
/// the initial value should be the identity of the operation. A scan also keeps every
/// intermediate result.
///
/// @par Example
/// @snippet algo/example_fold.c ops
/// @snippet algo/example_fold.c fold
/// @{

/// @name fold
/// @{

/// folds the span into 'acc', front to back
/// @param s the span
/// @param[in,out] acc the accumulator
/// @param fold called once per elem
/// @param ctx handed to 'fold'
/// @bigo{n}
TDA_API
void tda_span_fold(tda_Span s, void *acc, tda_Fold fold, void *ctx);

/// folds the span into 'acc', back to front
/// @param s the span
/// @param[in,out] acc the accumulator
/// @param fold called once per elem
/// @param ctx handed to 'fold'
/// @bigo{n}
TDA_API
void tda_span_fold_back(tda_Span s, void *acc, tda_Fold fold, void *ctx);

/// @}

/// @name scan
/// @{

/// running totals: dst[0] = src[0], then dst[i] = op(dst[i - 1], src[i])
/// @param dst asserts the same len as 'src'; may be 'src', since step i reads dst[i - 1],
///            already final, and src[i], not yet written. 'op' then gets src[i] as both
///            'dst' and 'rhs'; see tda_BinOp
/// @param src the source
/// @param op combines a running result with the next elem
/// @param ctx handed to 'op'
/// @bigo{n}
TDA_API
void tda_span_partial_sum(tda_SpanMut dst, tda_Span src, tda_BinOp op, void *ctx);

/// steps between neighbours: dst[0] = src[0], then dst[i] = op(src[i], src[i - 1])
/// @param dst asserts the same len as 'src'; may be 'src', since the steps go from the
///            back and step i reads src[i] and src[i - 1], neither written yet. 'op' then
///            gets src[i] as both 'dst' and 'lhs'; see tda_BinOp
/// @param src the source
/// @param op combines an elem with the one before it; with the inverse of the op given
///           to tda_span_partial_sum, this undoes it
/// @param ctx handed to 'op'
/// @bigo{n}
TDA_API
void tda_span_adjacent_difference(tda_SpanMut dst, tda_Span src, tda_BinOp op, void *ctx);

/// @}

/// @}
