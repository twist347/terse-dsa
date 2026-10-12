#pragma once

#include "tda/algo/fn.h"
#include "tda/core/export.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"

#include <stddef.h>

/// @file

/// @defgroup algo_copy algo/copy
/// @ingroup algo
/// @brief moving elems from one span to another, as the bytes they are
///
/// Both spans must have the same elem_size: this copies, it does not convert. Changing
/// the elem type on the way is algo/transform.
///
/// @par Example
/// @snippet algo/example_copy.c pred
/// @snippet algo/example_copy.c copy
/// @{

/// @name copy
/// @{

/// copies 'src' over 'dst'
/// @param dst where they go; asserts the same len and elem_size as 'src'
/// @param src where they come from; no overlap with 'dst' unless it is exactly 'dst'
/// @bigo{n}
TDA_API
void tda_span_copy(tda_SpanMut dst, tda_Span src);

/// copies the elems of 'src' that satisfy 'pred', packed to the front of 'dst'
/// @param dst where they go; asserts room for the ones that pass
/// @param src where they come from
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return how many were written; the rest of 'dst' is untouched
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_copy_if(tda_SpanMut dst, tda_Span src, tda_Pred pred, void *ctx);

/// tda_span_copy where the two may overlap
/// @param dst where they go; asserts the same len and elem_size as 'src'
/// @param src where they come from, overlapping 'dst' however it likes
/// @bigo{n} — the same copy through memmove instead of memcpy
TDA_API
void tda_span_copy_overlapping(tda_SpanMut dst, tda_Span src);

/// @}

/// @name sample
/// @{

/// copies a random pick of the elems of 'src' into 'dst', none twice, in the order 'src'
/// holds them
/// @param dst where they go; its len is how many to pick
/// @param src where they come from, left as it was; no overlap with 'dst'
/// @param rng where the pick comes from; the same seed replays the same pick
/// @return how many were written — dst.len, or src.len when 'src' has fewer, then copied
///         whole; the rest of 'dst' is untouched
/// @bigo{n} — selection sampling: each elem is taken with odds "still wanted / still
///            left", which makes every pick of that size equally likely. When 'src' may
///            change, tda_span_shuffle_prefix does it in O(count), at the cost of the order
[[nodiscard]] TDA_API
size_t tda_span_sample(tda_SpanMut dst, tda_Span src, tda_Rng *rng);

/// @}

/// @}
