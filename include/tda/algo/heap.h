#pragma once

#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_heap algo/heap
/// @ingroup algo
/// @brief a max-heap laid out in the span itself
///
/// A max-heap by 'cmp': no elem compares greater than its parent, so the largest is at
/// index 0. For a min-heap, pass a descending comparator; there is no second set. The
/// shape is the usual implicit tree with the children of i at 2i+1 and 2i+2, so only
/// the elems are stored and nothing is allocated.
///
/// @par Example
/// @snippet algo/example_heap.c build
/// @snippet algo/example_heap.c push
/// @{

/// @name build
/// @{

/// rearranges the whole span into a heap
/// @param s the span
/// @param cmp the order the heap is by
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{n}; built bottom-up, not by n insertions
TDA_API
void tda_span_make_heap(tda_SpanMut s, tda_Cmp cmp, void *ctx);

/// @}

/// @name push / pop
/// @{

/// sifts the last elem up into place
/// @param s the span; all but its last elem must be a heap by 'cmp'
/// @param cmp the order the heap is by
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{log n}. With a vec: tda_vec_push, then this
TDA_API
void tda_span_push_heap(tda_SpanMut s, tda_Cmp cmp, void *ctx);

/// moves the largest to the end and restores the heap over the elems before it
/// @param s the span; must be a heap by 'cmp'
/// @param cmp the order the heap is by
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{log n}. With a vec: this, then tda_vec_pop
TDA_API
void tda_span_pop_heap(tda_SpanMut s, tda_Cmp cmp, void *ctx);

/// @}

/// @name sort
/// @{

/// turns a heap into an ascending run by repeated pop
/// @param s the span; must be a heap by 'cmp'
/// @param cmp the order the heap is by
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{n log n}; heapsort is make_heap followed by this
TDA_API
void tda_span_sort_heap(tda_SpanMut s, tda_Cmp cmp, void *ctx);

/// @}

/// @name predicates
/// @{

/// whether the span is a heap
/// @param s the span; an empty one and a single elem are heaps
/// @param cmp the order in question
/// @param ctx passed to every call of 'cmp'; may be null
/// @return whether no elem compares greater than its parent
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_is_heap(tda_Span s, tda_Cmp cmp, void *ctx);

/// how far the span is a heap
/// @param s the span
/// @param cmp the order in question
/// @param ctx passed to every call of 'cmp'; may be null
/// @return the index of the first elem that breaks it, or s.len; the prefix before it
///         is a heap
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_is_heap_until(tda_Span s, tda_Cmp cmp, void *ctx);

/// @}

/// @}
