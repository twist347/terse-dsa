#pragma once

#include "tda/algo/fn.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_fold algo/fold
/// @ingroup algo
/// @brief folding a span into one value, and keeping the steps
///
/// The accumulator is the caller's and keeps the caller's type, so a span of int32_t
/// folds into an int64_t, a count, or a struct. An empty span leaves it untouched, which
/// makes the initial value the identity of the operation. A scan keeps what a fold throws
/// away: every intermediate result.
///
/// @par Example
/// @snippet algo/example_fold.c ops
/// @snippet algo/example_fold.c fold
/// @{

/// @name fold
/// @{

/// folds the span into 'acc', front to back
/// @param s the span
/// @param[in,out] acc the accumulator, the caller's own and of the caller's own type
/// @param fold called once per elem
/// @param ctx handed to 'fold'
/// @bigo{n}
TDA_API
void tda_span_fold(tda_Span s, void *acc, tda_Fold fold, void *ctx);

/// folds the span into 'acc', back to front
/// @param s the span
/// @param[in,out] acc the accumulator, the caller's own and of the caller's own type
/// @param fold called once per elem
/// @param ctx handed to 'fold'
/// @bigo{n}
TDA_API
void tda_span_fold_back(tda_Span s, void *acc, tda_Fold fold, void *ctx);

/// @}

/// @name scan
/// @{

/// running totals: dst[0] = src[0], then dst[i] = op(dst[i - 1], src[i])
/// @param dst where they go; asserts the same len as 'src'. May be 'src' itself: the step
///            reads dst[i - 1], which is already final, and src[i], which is not written
///            until that same step
/// @param src where they come from
/// @param op what combines a running result with the next elem
/// @param ctx handed to 'op'
/// @bigo{n}
TDA_API
void tda_span_partial_sum(tda_SpanMut dst, tda_Span src, tda_BinOp op, void *ctx);

/// steps between neighbours: dst[0] = src[0], then dst[i] = op(src[i], src[i - 1])
/// @param dst where they go; asserts the same len as 'src', and must not overlap it
/// @param src where they come from
/// @param op what combines an elem with the one before it; the inverse of
///           tda_span_partial_sum when 'op' is the inverse of its op
/// @param ctx handed to 'op'
/// @bigo{n}
TDA_API
void tda_span_adjacent_difference(tda_SpanMut dst, tda_Span src, tda_BinOp op, void *ctx);

/// @}

/// @}
