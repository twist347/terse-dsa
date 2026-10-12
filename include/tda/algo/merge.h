#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_merge algo/merge
/// @ingroup algo
/// @brief joining two sorted runs into one
///
/// Both are stable: among equal elems, those of the left run come first. To finish
/// sorting a partly sorted span, find the boundary with tda_span_is_sorted_until and
/// merge.
///
/// @par Example
/// @snippet algo/example_merge.c merge
/// @{

/// @name merge
/// @{

/// writes two sorted spans into 'dst' as one sorted run
/// @param dst must hold a.len + b.len and overlap neither source
/// @param a one sorted run
/// @param b the other, sorted by the same 'cmp'
/// @param cmp the order both are in
/// @bigo{n}
TDA_API
void tda_span_merge(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp);

/// joins the two sorted runs s[0, mid) and s[mid, len) in place
/// @param s the span; both halves must be sorted by 'cmp'
/// @param mid where the second run starts
/// @param cmp the order both are in
/// @param al for a buffer, or null. With one, the shorter run (at most half) is copied
///           aside and merged back; without one, or if allocation fails, a slower
///           path that needs no memory is used
/// @bigo{n} with a buffer, n log n without. A failed allocation only costs time, so
///        nothing is returned
TDA_API
void tda_span_inplace_merge(tda_SpanMut s, size_t mid, tda_Cmp cmp, tda_Al *al);

/// @}

/// @}
