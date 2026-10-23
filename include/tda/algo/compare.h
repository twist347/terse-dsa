#pragma once

#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_compare algo/compare
/// @ingroup algo
/// @brief comparing two spans against each other
///
/// The operands are 'a' and 'b': neither is a receiver. Both must have the same
/// elem layout; each function says how it treats differing lengths.
///
/// @par Example
/// @snippet algo/example_compare.c compare
/// @{

/// @name equality
/// @{

/// whether the two hold the same bytes
/// @param a one span
/// @param b the other
/// @return whether the lengths match and the bytes do. This is memcmp: -0.0 and +0.0
///         differ (unlike tda_eq_f32), and struct padding counts
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_eq(tda_Span a, tda_Span b);

/// whether the two hold equal elems under 'eq'
/// @param a one span
/// @param b the other
/// @param eq the equality; reflexive, since two views of the same elems are equal
///           without asking it
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether the lengths match and every pair does
/// @bigo{n}; stops at the first unequal pair
[[nodiscard]] TDA_API
bool tda_span_eq_by(tda_Span a, tda_Span b, tda_Eq eq, void *ctx);

/// where the two first differ
/// @param a one span
/// @param b the other
/// @param eq the equality; reflexive, since two views of the same elems do not differ
///           without asking it
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx the first index where they differ, written only when they do
/// @return whether they differ anywhere in their common prefix
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_mismatch(tda_Span a, tda_Span b, tda_Eq eq, void *ctx, size_t *out_idx);

/// whether the two hold the same elems in any order, each as many times
/// @param a one span
/// @param b the other; a different length gives false
/// @param eq must be a true equivalence (reflexive, symmetric, transitive), or the
///           counts mean nothing
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether one is a rearrangement of the other
/// @bigo{n^2} worst, allocating nothing: each distinct elem is counted on both sides.
///          The common prefix is skipped first, so equal spans cost O(n)
[[nodiscard]] TDA_API
bool tda_span_is_permutation(tda_Span a, tda_Span b, tda_Eq eq, void *ctx);

/// @}

/// @name ordering
/// @{

/// orders the two lexicographically
/// @param a one span
/// @param b the other
/// @param cmp the elem order
/// @param ctx passed to every call of 'cmp'; may be null
/// @return negative, zero or positive as 'a' orders before, with, or after 'b'. The first
///         differing pair decides; otherwise the shorter orders first
/// @bigo{n}
[[nodiscard]] TDA_API
int tda_span_cmp(tda_Span a, tda_Span b, tda_Cmp cmp, void *ctx);

/// @}

/// @}
