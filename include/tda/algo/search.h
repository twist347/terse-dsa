#pragma once

#include "tda/algo/fn.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

/// @file

/// @defgroup algo_search algo/search
/// @ingroup algo
/// @brief looking for something in a span, and answering where it is
///
/// Two verbs: a find scans and is valid over any span, a search descends and needs the
/// span sorted by the same cmp — so tda_span_binary_search is the only search here. The
/// suffix says what is looked for, never from which end: backwards is _last.
///
/// The boundary ops descend too but carry neither verb: they answer where, not whether.
/// A find answers through 'out_idx' and returns whether it hit; a miss is an answer, not
/// an error, and leaves 'out_idx' alone.
///
/// @par Example
/// @snippet algo/example_search.c pred
/// @snippet algo/example_search.c find
/// @snippet algo/example_search.c sorted
/// @{

/// @name find
/// @{

/// the first elem equal to 'key'
/// @param s the span
/// @param key the address of the value
/// @param eq the equality
/// @param[out] out_idx where, written only on a hit
/// @return whether it is there
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find(tda_Span s, const void *key, tda_Eq eq, size_t *out_idx);

/// the first elem satisfying 'pred'
/// @param s the span
/// @param pred the test
/// @param ctx handed to 'pred'
/// @param[out] out_idx where, written only on a hit
/// @return whether there is one
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find_if(tda_Span s, tda_Pred pred, void *ctx, size_t *out_idx);

/// the first place 'sub' occurs in 's', elem by elem
/// @param s the span to look in
/// @param sub what to look for; empty occurs at 0
/// @param eq the equality
/// @param[out] out_idx where it starts, on a hit
/// @return whether it occurs
/// @bigo{n*m} — a plain scan, no preprocessing
[[nodiscard]] TDA_API
bool tda_span_find_sub(tda_Span s, tda_Span sub, tda_Eq eq, size_t *out_idx);

/// the last place 'sub' occurs in 's'
/// @param s the span to look in
/// @param sub what to look for; empty occurs at s.len
/// @param eq the equality
/// @param[out] out_idx where it starts, on a hit
/// @return whether it occurs
/// @bigo{n*m}
[[nodiscard]] TDA_API
bool tda_span_find_sub_last(tda_Span s, tda_Span sub, tda_Eq eq, size_t *out_idx);

/// the first run of 'count' elems in a row all equal to 'key'
/// @param s the span
/// @param key the address of the value
/// @param count how long the run must be; 0 is found at 0
/// @param eq the equality
/// @param[out] out_idx where the run starts, on a hit
/// @return whether there is one
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find_run(tda_Span s, const void *key, size_t count, tda_Eq eq,
                       size_t *out_idx);

/// the first elem of 's' equal to any elem of 'set'
/// @param s the span to look in
/// @param set what counts as a hit; unordered, duplicates allowed
/// @param eq the equality
/// @param[out] out_idx where, written only on a hit
/// @return whether there is one
/// @bigo{n*k} — k is the length of 'set'
[[nodiscard]] TDA_API
bool tda_span_find_any_of(tda_Span s, tda_Span set, tda_Eq eq, size_t *out_idx);

/// the first elem equal to the one right after it
/// @param s the span
/// @param eq the equality
/// @param[out] out_idx where the pair starts, on a hit
/// @return whether two equal elems are adjacent anywhere
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find_adjacent(tda_Span s, tda_Eq eq, size_t *out_idx);

/// tda_span_find without the index
/// @param s the span
/// @param key the address of the value
/// @param eq the equality
/// @return whether it is there
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_contains(tda_Span s, const void *key, tda_Eq eq);

/// @}

/// @name count
/// @{

/// how many elems equal 'key'
/// @param s the span
/// @param key the address of the value
/// @param eq the equality
/// @return the count, 0 when none
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_count(tda_Span s, const void *key, tda_Eq eq);

/// how many elems satisfy 'pred'
/// @param s the span
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return the count, 0 when none
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_count_if(tda_Span s, tda_Pred pred, void *ctx);

/// @}

/// @name binary search
/// @{

/// where 'key' would go, before any elem equal to it
/// @param s the span, sorted by 'cmp'
/// @param key the address of the value
/// @param cmp the order
/// @return the first index whose elem does not order before 'key', or s.len
/// @bigo{log n}
[[nodiscard]] TDA_API
size_t tda_span_lower_bound(tda_Span s, const void *key, tda_Cmp cmp);

/// where 'key' would go, after any elem equal to it
/// @param s the span, sorted by 'cmp'
/// @param key the address of the value
/// @param cmp the order
/// @return the first index whose elem orders after 'key', or s.len
/// @bigo{log n}
[[nodiscard]] TDA_API
size_t tda_span_upper_bound(tda_Span s, const void *key, tda_Cmp cmp);

/// whether 'key' is in a sorted span, and where
/// @param s the span, sorted by 'cmp'
/// @param key the address of the value
/// @param cmp the order
/// @param[out] out_idx an index holding it, on a hit; with duplicates, no particular one
/// @return whether it is there
/// @bigo{log n}
[[nodiscard]] TDA_API
bool tda_span_binary_search(tda_Span s, const void *key, tda_Cmp cmp, size_t *out_idx);

/// A half-open index range, [lo, hi).
typedef struct {
    size_t lo; ///< the first index in the range
    size_t hi; ///< one past the last; lo == hi is empty
} tda_Range;

/// every elem equal to 'key', as one range
/// @param s the span, sorted by 'cmp'
/// @param key the address of the value
/// @param cmp the order
/// @return [lower_bound, upper_bound), empty when 'key' is absent
/// @bigo{log n}
[[nodiscard]] TDA_API
tda_Range tda_span_equal_range(tda_Span s, const void *key, tda_Cmp cmp);

/// the boundary of a span partitioned by 'pred' — tda_span_lower_bound generalized
/// @param s the span; 'pred' must be monotonic over it, true for a prefix
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return the first index failing 'pred', or s.len
/// @bigo{log n}
[[nodiscard]] TDA_API
size_t tda_span_partition_point(tda_Span s, tda_Pred pred, void *ctx);

/// @}

/// @name predicates
/// @{

/// whether every elem satisfies 'pred'
/// @param s the span; an empty one is true, there being nothing to fail
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return whether they all do
/// @bigo{n} — it stops early
[[nodiscard]] TDA_API
bool tda_span_all_of(tda_Span s, tda_Pred pred, void *ctx);

/// whether some elem satisfies 'pred'
/// @param s the span; an empty one is false
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return whether at least one does
/// @bigo{n} — it stops early
[[nodiscard]] TDA_API
bool tda_span_any_of(tda_Span s, tda_Pred pred, void *ctx);

/// whether no elem satisfies 'pred'
/// @param s the span; an empty one is true
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return whether none does
/// @bigo{n} — it stops early
[[nodiscard]] TDA_API
bool tda_span_none_of(tda_Span s, tda_Pred pred, void *ctx);

/// @}

/// @name extremes
/// @{

/// the smallest elem
/// @param s the span; asserts it is not empty, there being no index to name
/// @param cmp the order
/// @return its index; a tie goes to the first
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_min_elem(tda_Span s, tda_Cmp cmp);

/// the largest elem
/// @param s the span; asserts it is not empty
/// @param cmp the order
/// @return its index; a tie goes to the first, as in std::max_element
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_max_elem(tda_Span s, tda_Cmp cmp);

/// Where the smallest and the largest elems are.
typedef struct {
    size_t min; ///< the index of the smallest
    size_t max; ///< the index of the largest
} tda_MinMax;

/// both extremes, in one pass
/// @param s the span; asserts it is not empty
/// @param cmp the order
/// @return the two indices, each a tie going to the first — unlike std::minmax_element,
///         which gives the last of the largest
/// @bigo{n}
[[nodiscard]] TDA_API
tda_MinMax tda_span_minmax_elem(tda_Span s, tda_Cmp cmp);

/// @}

/// @}
