#pragma once

#include <stddef.h>

/// @file

/// @defgroup algo_fn algo/fn
/// @ingroup algo
/// @brief the function-pointer types algo is customized through
///
/// Each takes its elems by address plus a 'ctx' that the caller owns and algo never
/// reads, so no globals are needed. Ordering and equality live in core/cmp, which also
/// has ready-made comparators.
///
/// @{

/// Whether an elem passes. Takes the elem and the caller's 'ctx'.
typedef bool (*tda_Pred)(const void *elem, void *ctx);

/// Folds one elem into the accumulator. 'acc' belongs to the caller and may be of any
/// type, so a span of int32_t can fold into an int64_t.
typedef void (*tda_Fold)(void *acc, const void *elem, void *ctx);

/// Produces the elem for position 'idx', writing it to 'dst'.
typedef void (*tda_Gen)(void *dst, size_t idx, void *ctx);

/// Produces the elem written to 'dst' from the one at 'src'.
typedef void (*tda_UnOp)(void *dst, const void *src, void *ctx);

/// Combines two elems into 'dst': 'dst' is written, 'a' and 'b' are read.
typedef void (*tda_BinOp)(void *dst, const void *a, const void *b, void *ctx);

/// @}
