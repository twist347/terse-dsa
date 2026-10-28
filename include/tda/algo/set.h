#pragma once

#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

#include <stddef.h>

/// @file

/// @defgroup algo_set algo/set
/// @ingroup algo
/// @brief set operations over two spans already sorted by the same cmp
///
/// Because the inputs are sorted, each op walks both spans once in linear time. The
/// output is sorted by the same cmp, so the ops compose with each other and with
/// tda_span_merge.
///
/// These are multiset ops: equal elems are counted, not collapsed. If 'a' holds m copies
/// of a value and 'b' holds n, the result holds:
///
///     union                  max(m, n)      the m from 'a', then n - m more from 'b'
///     intersection           min(m, n)      taken from 'a'
///     difference             m - n, or 0    taken from 'a'
///     symmetric difference   |m - n|        from whichever side has more
///
/// For plain set behaviour, run the spans through tda_span_unique first.
///
/// Each op writes into 'dst' and returns how many elems it wrote. The result length is
/// not known in advance, so 'dst' must fit the worst case given on each op. 'dst' must
/// overlap neither source, since the ops write while they read.
///
/// @par Example
/// @snippet algo/example_set.c ops
/// @{

/// @name set ops
/// @{

/// every elem of both, keeping the greater count of equal ones
/// @param dst must hold a.len + b.len
/// @param a one sorted span
/// @param b the other, sorted by the same 'cmp'
/// @param cmp the order both are in
/// @param ctx passed to every call of 'cmp'; may be null
/// @return how many elems were written
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_set_union(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp, void *ctx);

/// the elems present in both
/// @param dst must hold the shorter of the two
/// @param a one sorted span
/// @param b the other, sorted by the same 'cmp'
/// @param cmp the order both are in
/// @param ctx passed to every call of 'cmp'; may be null
/// @return how many elems were written
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_set_intersection(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp, void *ctx);

/// the elems of 'a' not matched in 'b'
/// @param dst must hold a.len
/// @param a the span to take from
/// @param b the span to subtract, sorted by the same 'cmp'
/// @param cmp the order both are in
/// @param ctx passed to every call of 'cmp'; may be null
/// @return how many elems were written
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_set_difference(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp, void *ctx);

/// the elems present in exactly one of the two
/// @param dst must hold a.len + b.len
/// @param a one sorted span
/// @param b the other, sorted by the same 'cmp'
/// @param cmp the order both are in
/// @param ctx passed to every call of 'cmp'; may be null
/// @return how many elems were written
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_set_symmetric_difference(tda_SpanMut dst, tda_Span a, tda_Span b, tda_Cmp cmp, void *ctx);

/// @}

/// @name predicates
/// @{

/// the subset test, with the containing span first
/// @param sup the span that may contain the other
/// @param sub the span to look for; an empty one is included in anything. Each elem is a
///            key, the right side of 'cmp', so it need not share the layout of 'sup'
/// @param cmp the order both are in, called as cmp(elem of sup, elem of sub)
/// @param ctx passed to every call of 'cmp'; may be null
/// @return whether 'sup' contains every elem of 'sub', counting duplicates
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_includes(tda_Span sup, tda_Span sub, tda_Cmp cmp, void *ctx);

/// @}

/// @}
