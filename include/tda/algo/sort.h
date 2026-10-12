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
/// @par Example
/// @snippet algo/example_sort.c sort
/// @snippet algo/example_sort.c partial
/// @{

/// @name sort
/// @{

/// insertion sort; worth it only on a span that is nearly sorted
/// @param s the span
/// @param cmp the order to put it in
/// @bigo{n^2}; O(n) on a sorted span
TDA_API
void tda_span_insertion_sort(tda_SpanMut s, tda_Cmp cmp);

/// sorts in place, unstably
/// @param s the span
/// @param cmp the order to put it in
/// @bigo{n log n}; O(n) on few distinct values, thanks to the three-way split
TDA_API
void tda_span_sort(tda_SpanMut s, tda_Cmp cmp);

/// sorts in place, keeping equal elems in their original order
/// @param s the span
/// @param cmp the order to put it in
/// @param al for the span-sized buffer of a bottom-up merge sort
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buffer cannot be allocated; 's' is unchanged
/// @bigo{n log n}
[[nodiscard]] TDA_API
tda_Status tda_span_sort_stable(tda_SpanMut s, tda_Cmp cmp, tda_Al *al);

/// puts the smallest 'count' elems, sorted, at the front
/// @param s the span
/// @param count asserts count <= s.len
/// @param cmp the order to take them by
/// @bigo{n + k log k} expected with k = 'count', n log n worst. The rest of the span is
///        left in no particular order
TDA_API
void tda_span_partial_sort(tda_SpanMut s, size_t count, tda_Cmp cmp);

/// puts at 'nth' the elem that would be there in sorted order
/// @param s the span; asserts it is not empty
/// @param nth asserts nth < s.len. Nothing before it is greater, nothing after it is
///            less, and neither side is sorted
/// @param cmp the order in question
/// @bigo{n} expected, n log n worst. A quickselect that falls back to tda_span_sort
///        on a range whose pivots keep going bad
TDA_API
void tda_span_nth_elem(tda_SpanMut s, size_t nth, tda_Cmp cmp);

/// @}

/// @name predicates
/// @{

/// whether the span is in order
/// @param s the span; an empty one and a single elem are in order
/// @param cmp the order in question
/// @return whether no elem orders before the one preceding it
/// @bigo{n}; stops at the first pair out of order
[[nodiscard]] TDA_API
bool tda_span_is_sorted(tda_Span s, tda_Cmp cmp);

/// how far the span is in order
/// @param s the span
/// @param cmp the order in question
/// @return the index of the first elem out of order, or s.len. The prefix before it is
///         sorted, ready for tda_span_inplace_merge
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_is_sorted_until(tda_Span s, tda_Cmp cmp);

/// @}

/// @}
