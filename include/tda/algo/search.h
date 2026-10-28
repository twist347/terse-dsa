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
/// A find scans and works on any span; a search bisects and needs the span sorted by the
/// same cmp, so tda_span_binary_search is the only search here. Backward finds put the
/// direction first (find_last_if, find_last_sub), as C++23 does.
///
/// The bound ops also bisect but answer where, not whether. A find writes 'out_idx' and
/// returns whether it hit; a miss is not an error and leaves 'out_idx' unchanged.
///
/// An op that takes a key calls 'eq' or 'cmp' with the elem on the left and the key on
/// the right, never the other way, so the key need not be an elem: a span of users
/// sorted by id can be searched with the id alone.
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
/// @param key the value to look for
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx the index, written only on a hit
/// @return whether it is there
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find(tda_Span s, const void *key, tda_Eq eq, void *ctx, size_t *out_idx);

/// the first elem satisfying 'pred'
/// @param s the span
/// @param pred the test
/// @param ctx handed to 'pred'
/// @param[out] out_idx the index, written only on a hit
/// @return whether there is one
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find_if(tda_Span s, tda_Pred pred, void *ctx, size_t *out_idx);

/// the last elem equal to 'key'
/// @param s the span
/// @param key the value to look for
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx the index, written only on a hit
/// @return whether it is there
/// @bigo{n}; scans from the back, so a hit near the end is found early
[[nodiscard]] TDA_API
bool tda_span_find_last(tda_Span s, const void *key, tda_Eq eq, void *ctx, size_t *out_idx);

/// the last elem satisfying 'pred'
/// @param s the span
/// @param pred the test, called back to front
/// @param ctx handed to 'pred'
/// @param[out] out_idx the index, written only on a hit
/// @return whether there is one
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find_last_if(tda_Span s, tda_Pred pred, void *ctx, size_t *out_idx);

/// the first place 'sub' occurs in 's', elem by elem
/// @param s the span to look in
/// @param sub what to look for; empty occurs at 0. Each elem is a key, the right side of
///            'eq', so it need not share the layout of 's'
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx where it starts, on a hit
/// @return whether it occurs
/// @bigo{n*m}; a plain scan, no preprocessing
[[nodiscard]] TDA_API
bool tda_span_find_sub(tda_Span s, tda_Span sub, tda_Eq eq, void *ctx, size_t *out_idx);

/// the last place 'sub' occurs in 's'
/// @param s the span to look in
/// @param sub what to look for; empty occurs at s.len. Each elem is a key, as in
///            tda_span_find_sub
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx where it starts, on a hit
/// @return whether it occurs
/// @bigo{n*m}
[[nodiscard]] TDA_API
bool tda_span_find_last_sub(tda_Span s, tda_Span sub, tda_Eq eq, void *ctx, size_t *out_idx);

/// the first run of 'count' elems in a row all equal to 'key'
/// @param s the span
/// @param key the value to look for
/// @param count the run length; 0 is found at 0
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx where the run starts, on a hit
/// @return whether there is one
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find_run(tda_Span s, const void *key, size_t count, tda_Eq eq, void *ctx,
                       size_t *out_idx);

/// the first elem of 's' equal to any elem of 'set'
/// @param s the span to look in
/// @param set what counts as a hit; unordered, duplicates allowed. Each is a key, so it
///            need not share the layout of 's'
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx the index, written only on a hit
/// @return whether there is one
/// @bigo{n*k}, k being set.len
[[nodiscard]] TDA_API
bool tda_span_find_any_of(tda_Span s, tda_Span set, tda_Eq eq, void *ctx, size_t *out_idx);

/// the first elem equal to the one right after it
/// @param s the span
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx where the pair starts, on a hit
/// @return whether two equal elems are adjacent anywhere
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_find_adjacent(tda_Span s, tda_Eq eq, void *ctx, size_t *out_idx);

/// tda_span_find without the index
/// @param s the span
/// @param key the value to look for
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether it is there
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_contains(tda_Span s, const void *key, tda_Eq eq, void *ctx);

/// @}

/// @name count
/// @{

/// how many elems equal 'key'
/// @param s the span
/// @param key the value to look for
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @return the count, 0 when none
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_count(tda_Span s, const void *key, tda_Eq eq, void *ctx);

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
/// @param key the value to look for
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @return the first index whose elem does not order before 'key', or s.len
/// @bigo{log n}
[[nodiscard]] TDA_API
size_t tda_span_lower_bound(tda_Span s, const void *key, tda_Cmp cmp, void *ctx);

/// where 'key' would go, after any elem equal to it
/// @param s the span, sorted by 'cmp'
/// @param key the value to look for
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @return the first index whose elem orders after 'key', or s.len
/// @bigo{log n}
[[nodiscard]] TDA_API
size_t tda_span_upper_bound(tda_Span s, const void *key, tda_Cmp cmp, void *ctx);

/// whether 'key' is in a sorted span, and where
/// @param s the span, sorted by 'cmp'
/// @param key the value to look for
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @param[out] out_idx where it is, on a hit; with duplicates, the first of them
/// @return whether it is there
/// @bigo{log n}
[[nodiscard]] TDA_API
bool tda_span_binary_search(tda_Span s, const void *key, tda_Cmp cmp, void *ctx, size_t *out_idx);

/// A half-open index range, [lo, hi).
typedef struct {
    size_t lo; ///< the first index in the range
    size_t hi; ///< one past the last; lo == hi is empty
} tda_Range;

/// every elem equal to 'key', as one range
/// @param s the span, sorted by 'cmp'
/// @param key the value to look for
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @return [lower_bound, upper_bound), empty when 'key' is absent
/// @bigo{log n}
[[nodiscard]] TDA_API
tda_Range tda_span_equal_range(tda_Span s, const void *key, tda_Cmp cmp, void *ctx);

/// the boundary of a span partitioned by 'pred'; a generalized tda_span_lower_bound
/// @param s the span; 'pred' must hold for a prefix and fail for the rest
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
/// @param s the span; an empty one is true
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return whether they all do
/// @bigo{n}; stops early
[[nodiscard]] TDA_API
bool tda_span_all_of(tda_Span s, tda_Pred pred, void *ctx);

/// whether some elem satisfies 'pred'
/// @param s the span; an empty one is false
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return whether at least one does
/// @bigo{n}; stops early
[[nodiscard]] TDA_API
bool tda_span_any_of(tda_Span s, tda_Pred pred, void *ctx);

/// whether no elem satisfies 'pred'
/// @param s the span; an empty one is true
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return whether none does
/// @bigo{n}; stops early
[[nodiscard]] TDA_API
bool tda_span_none_of(tda_Span s, tda_Pred pred, void *ctx);

/// whether 's' opens with 'prefix'
/// @param s the span to look in
/// @param prefix what it should open with; empty is true, longer than 's' is false. Each
///               elem is a key, as in tda_span_find_sub
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether the first prefix.len elems of 's' equal those of 'prefix'
/// @bigo{m}, m being prefix.len; stops at the first differing pair
[[nodiscard]] TDA_API
bool tda_span_starts_with(tda_Span s, tda_Span prefix, tda_Eq eq, void *ctx);

/// whether 's' closes with 'suffix'
/// @param s the span to look in
/// @param suffix what it should close with; empty is true, longer than 's' is false. Each
///               elem is a key, as in tda_span_find_sub
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @return whether the last suffix.len elems of 's' equal those of 'suffix'
/// @bigo{m}, m being suffix.len; stops at the first differing pair
[[nodiscard]] TDA_API
bool tda_span_ends_with(tda_Span s, tda_Span suffix, tda_Eq eq, void *ctx);

/// @}

/// @name extremes
/// @{

/// the smallest elem
/// @param s the span; asserts it is not empty
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @return its index; a tie goes to the first
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_min_elem(tda_Span s, tda_Cmp cmp, void *ctx);

/// the largest elem
/// @param s the span; asserts it is not empty
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @return its index; a tie goes to the first, as in std::max_element
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_max_elem(tda_Span s, tda_Cmp cmp, void *ctx);

/// Where the smallest and the largest elems are.
typedef struct {
    size_t min; ///< the index of the smallest
    size_t max; ///< the index of the largest
} tda_MinMax;

/// both extremes, in one pass
/// @param s the span; asserts it is not empty
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @return the two indices; ties go to the first, unlike std::minmax_element, which
///         gives the last of the largest
/// @bigo{n}
[[nodiscard]] TDA_API
tda_MinMax tda_span_minmax_elem(tda_Span s, tda_Cmp cmp, void *ctx);

/// @}

/// @name macros
/// @{

/// tda_span_find from a value rather than an address: TDA_SPAN_FIND(int32_t, s, 7, eq,
/// nullptr, &idx)
/// @param K the key's type, which need not be the elem's, so nothing checks it against
///          the span; 'key' may be any K, a struct literal in parentheses too
/// @param s the span
/// @param key the value to look for
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx the index, written only on a hit
/// @bigo{n}
#define TDA_SPAN_FIND(K, s, key, eq, ctx, out_idx) \
    tda_span_find((s), &(typeof(K)[1]){ (key) }[0], (eq), (ctx), (out_idx))

/// tda_span_find_last from a value rather than an address
/// @copydetails TDA_SPAN_FIND
#define TDA_SPAN_FIND_LAST(K, s, key, eq, ctx, out_idx) \
    tda_span_find_last((s), &(typeof(K)[1]){ (key) }[0], (eq), (ctx), (out_idx))

/// tda_span_find_run from a value rather than an address
/// @param K the key's type, which need not be the elem's, so nothing checks it against
///          the span; 'key' may be any K, a struct literal in parentheses too
/// @param s the span
/// @param key the value to look for
/// @param count the run length
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @param[out] out_idx where the run starts, on a hit
/// @bigo{n}
#define TDA_SPAN_FIND_RUN(K, s, key, count, eq, ctx, out_idx) \
    tda_span_find_run((s), &(typeof(K)[1]){ (key) }[0], (count), (eq), (ctx), (out_idx))

/// tda_span_contains from a value rather than an address
/// @param K the key's type, which need not be the elem's, so nothing checks it against
///          the span; 'key' may be any K, a struct literal in parentheses too
/// @param s the span
/// @param key the value to look for
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @bigo{n}
#define TDA_SPAN_CONTAINS(K, s, key, eq, ctx) \
    tda_span_contains((s), &(typeof(K)[1]){ (key) }[0], (eq), (ctx))

/// tda_span_count from a value rather than an address
/// @copydetails TDA_SPAN_CONTAINS
#define TDA_SPAN_COUNT(K, s, key, eq, ctx) \
    tda_span_count((s), &(typeof(K)[1]){ (key) }[0], (eq), (ctx))

/// tda_span_lower_bound from a value rather than an address
/// @param K the key's type, which need not be the elem's, so nothing checks it against
///          the span; 'key' may be any K, a struct literal in parentheses too
/// @param s the span, sorted by 'cmp'
/// @param key the value to look for
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @bigo{log n}
#define TDA_SPAN_LOWER_BOUND(K, s, key, cmp, ctx) \
    tda_span_lower_bound((s), &(typeof(K)[1]){ (key) }[0], (cmp), (ctx))

/// tda_span_upper_bound from a value rather than an address
/// @copydetails TDA_SPAN_LOWER_BOUND
#define TDA_SPAN_UPPER_BOUND(K, s, key, cmp, ctx) \
    tda_span_upper_bound((s), &(typeof(K)[1]){ (key) }[0], (cmp), (ctx))

/// tda_span_equal_range from a value rather than an address
/// @copydetails TDA_SPAN_LOWER_BOUND
#define TDA_SPAN_EQUAL_RANGE(K, s, key, cmp, ctx) \
    tda_span_equal_range((s), &(typeof(K)[1]){ (key) }[0], (cmp), (ctx))

/// tda_span_binary_search from a value rather than an address
/// @param K the key's type, which need not be the elem's, so nothing checks it against
///          the span; 'key' may be any K, a struct literal in parentheses too
/// @param s the span, sorted by 'cmp'
/// @param key the value to look for
/// @param cmp the order
/// @param ctx passed to every call of 'cmp'; may be null
/// @param[out] out_idx where it is, on a hit
/// @bigo{log n}
#define TDA_SPAN_BINARY_SEARCH(K, s, key, cmp, ctx, out_idx) \
    tda_span_binary_search((s), &(typeof(K)[1]){ (key) }[0], (cmp), (ctx), (out_idx))

/// @}

/// @}
