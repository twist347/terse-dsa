#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"
#include "tda/core/status.h"

/// @file

/// @defgroup algo_sort algo/sort
/// @ingroup algo
/// @brief putting a span in order, and asking whether it already is
///
/// tda_span_sort is the default: a quicksort with a ninther pivot and a three-way split,
/// in place and allocating nothing. It is not stable. Stability needs a buffer, so
/// tda_span_sort_stable takes an allocator and returns a tda_Status.
///
/// partial_sort is for when only the first few elems matter, nth_elem for when only one
/// position does. The predicates check order without changing anything.
///
/// cmp has to be a strict weak ordering: consistent answers for the same pair, and
/// transitive, equal included. tda_cmp_* are, floats too. Under one that is not, the order
/// that comes out is unspecified, but every elem stays in the span exactly once and
/// nothing outside it is read.
///
/// @par Example
/// @snippet algo/example_sort.c sort
/// @snippet algo/example_sort.c argsort_cmp
/// @snippet algo/example_sort.c argsort
/// @snippet algo/example_sort.c partial
/// @{

/// @name sort
/// @{

/// insertion sort, stable: equal elems keep their order; worth it only on a span that
/// is nearly sorted
/// @param s the span
/// @param cmp the order to put it in
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{n^2}; O(n) on a sorted span
TDA_API
void tda_span_insertion_sort(tda_SpanMut s, tda_Cmp cmp, void *ctx);

/// sorts in place, unstably
/// @param s the span
/// @param cmp the order to put it in
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{n log n}; O(n) on few distinct values, thanks to the three-way split
TDA_API
void tda_span_sort(tda_SpanMut s, tda_Cmp cmp, void *ctx);

/// sorts in place, keeping equal elems in their original order
/// @param s the span
/// @param cmp the order to put it in
/// @param ctx passed to every call of 'cmp'; may be null
/// @param al for the span-sized buffer of a bottom-up merge sort
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buffer cannot be allocated; 's' is unchanged
/// @bigo{n log n}
[[nodiscard]] TDA_API
tda_Status tda_span_sort_stable(tda_SpanMut s, tda_Cmp cmp, void *ctx, tda_Al al);

/// puts the smallest 'count' elems, sorted, at the front
/// @param s the span
/// @param count asserts count <= s.len
/// @param cmp the order to take them by
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{n + k log k} expected with k = 'count', n log n worst. The rest of the span is
///        left in no particular order
TDA_API
void tda_span_partial_sort(tda_SpanMut s, size_t count, tda_Cmp cmp, void *ctx);

/// puts at 'nth' the elem that would be there in sorted order
/// @param s the span; asserts it is not empty
/// @param nth asserts nth < s.len. Nothing before it is greater, nothing after it is
///            less, and neither side is sorted
/// @param cmp the order in question
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{n} expected, n log n worst. A quickselect that falls back to tda_span_sort
///        on a range whose pivots keep going bad
TDA_API
void tda_span_nth_elem(tda_SpanMut s, size_t nth, tda_Cmp cmp, void *ctx);

/// @}

/// @name predicates
/// @{

/// whether the span is in order
/// @param s the span; an empty one and a single elem are in order
/// @param cmp the order in question
/// @param ctx passed to every call of 'cmp'; may be null
/// @return whether no elem orders before the one preceding it
/// @bigo{n}; stops at the first pair out of order
[[nodiscard]] TDA_API
bool tda_span_is_sorted(tda_Span s, tda_Cmp cmp, void *ctx);

/// how far the span is in order
/// @param s the span
/// @param cmp the order in question
/// @param ctx passed to every call of 'cmp'; may be null
/// @return the index of the first elem out of order, or s.len. The prefix before it is
///         sorted: sort the rest, and tda_span_inplace_merge joins the two
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_is_sorted_until(tda_Span s, tda_Cmp cmp, void *ctx);

/// @}

/// @}
