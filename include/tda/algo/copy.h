#pragma once

#include "tda/algo/fn.h"
#include "tda/core/export.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"

#include <stddef.h>

/// @file

/// @defgroup algo_copy algo/copy
/// @ingroup algo
/// @brief copying elems from one span to another as raw bytes
///
/// Both spans must have the same elem layout; nothing is converted. To change the elem
/// type, use algo/transform.
///
/// @par Example
/// @snippet algo/example_copy.c pred
/// @snippet algo/example_copy.c copy
/// @snippet algo/example_copy.c sample
/// @{

/// @name copy
/// @{

/// copies 'src' over 'dst'
/// @param dst asserts the same len and elem layout as 'src'
/// @param src must not overlap 'dst' unless it is exactly 'dst'
/// @bigo{n}
TDA_API
void tda_span_copy(tda_SpanMut dst, tda_Span src);

/// copies the elems of 'src' that satisfy 'pred', packed to the front of 'dst'
/// @param dst asserts room for the elems that pass
/// @param src the source
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return how many were written; the rest of 'dst' is untouched
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_copy_if(tda_SpanMut dst, tda_Span src, tda_Pred pred, void *ctx);

/// tda_span_copy where the two may overlap
/// @param dst asserts the same len and elem layout as 'src'
/// @param src may overlap 'dst' in any way
/// @bigo{n}; memmove instead of memcpy
TDA_API
void tda_span_copy_overlapping(tda_SpanMut dst, tda_Span src);

/// @}

/// @name sample
/// @{

/// copies a random selection of the elems of 'src' into 'dst', none twice, keeping
/// their order in 'src'
/// @param dst its len is how many to pick
/// @param src left unchanged; must not overlap 'dst'
/// @param rng the source of randomness; the same seed gives the same pick
/// @return how many were written: dst.len, or src.len when 'src' is shorter (then all
///         of it is copied); the rest of 'dst' is untouched
/// @bigo{n}. Selection sampling: each elem is taken with odds "still wanted / still
///          left", so every pick of that size is equally likely. If 'src' may be
///          changed, tda_span_shuffle_prefix takes O(count) but loses the order
[[nodiscard]] TDA_API
size_t tda_span_sample(tda_SpanMut dst, tda_Span src, tda_Rng *rng);

/// @}

/// @}
