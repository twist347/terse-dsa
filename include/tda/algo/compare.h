#pragma once

#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_compare algo/compare
/// @ingroup algo
/// @brief comparing two spans against each other
///
/// The operands are 'a' and 'b', not a receiver and an argument — neither is the one
/// being asked about. Same elem_size for both; differing lengths are each function's own
/// business.
///
/// @par Example
/// @snippet algo/example_compare.c compare
/// @{

/// @name equality
/// @{

/// whether the two hold the same bytes
/// @param a one span
/// @param b the other
/// @return whether the lengths match and the bytes do — being memcmp, it parts -0.0 from
///         +0.0 where tda_eq_f32 would not, and a struct's padding counts
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_eq(tda_Span a, tda_Span b);

/// whether the two hold equal elems under 'eq'
/// @param a one span
/// @param b the other
/// @param eq the equality
/// @return whether the lengths match and every pair does
/// @bigo{n} — it stops at the first pair that does not
[[nodiscard]] TDA_API
bool tda_span_eq_by(tda_Span a, tda_Span b, tda_Eq eq);

/// where the two first differ
/// @param a one span
/// @param b the other
/// @param eq the equality
/// @param[out] out_idx the first index where they differ, written only when they do
/// @return whether they differ anywhere in their common prefix
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_mismatch(tda_Span a, tda_Span b, tda_Eq eq, size_t *out_idx);

/// whether the two hold the same elems in any order, each as many times
/// @param a one span
/// @param b the other; a differing length is just false
/// @param eq the equality, which has to be one — reflexive, symmetric and transitive — for
///           counting under it to mean anything
/// @return whether one is a rearrangement of the other
/// @bigo{n^2} worst, with nothing allocated: each distinct elem is counted on both sides.
///          The common prefix is skipped first, so two spans that agree elem by elem cost
///          O(n)
[[nodiscard]] TDA_API
bool tda_span_is_permutation(tda_Span a, tda_Span b, tda_Eq eq);

/// @}

/// @name ordering
/// @{

/// orders the two the way a dictionary orders words
/// @param a one span
/// @param b the other
/// @param cmp the order elems are in
/// @return negative, zero or positive as 'a' orders before, with, or after 'b': the first
///         differing pair decides, failing which the shorter orders first
/// @bigo{n}
[[nodiscard]] TDA_API
int tda_span_cmp(tda_Span a, tda_Span b, tda_Cmp cmp);

/// @}

/// @}
