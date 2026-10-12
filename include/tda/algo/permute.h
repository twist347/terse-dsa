#pragma once

#include "tda/algo/fn.h"
#include "tda/alloc/alloc.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/rng.h"
#include "tda/core/span.h"
#include "tda/core/status.h"

#include <stddef.h>

/// @file

/// @defgroup algo_permute algo/permute
/// @ingroup algo
/// @brief rearranging a span without changing what it holds
///
/// Everything works in place without allocating, except the stable partition, which
/// takes an allocator and returns a tda_Status. The shuffles take a tda_Rng.
///
/// @par Example
/// @snippet algo/example_permute.c pred
/// @snippet algo/example_permute.c move
/// @snippet algo/example_permute.c shuffle
/// @snippet algo/example_permute.c partition
/// @{

/// @name move
/// @{

/// turns the span back to front
/// @param s the span
/// @bigo{n}
TDA_API
void tda_span_reverse(tda_SpanMut s);

/// rotates left so that the elem at 'mid' becomes the first
/// @param s the span
/// @param mid the new front; asserts mid <= s.len; 0 or s.len is a no-op
/// @bigo{n}; three reversals, no buffer
TDA_API
void tda_span_rotate(tda_SpanMut s, size_t mid);

/// exchanges the elems of two spans, position by position
/// @param a one span
/// @param b the other; asserts the same len and elem_size
/// @bigo{n}
TDA_API
void tda_span_swap_ranges(tda_SpanMut a, tda_SpanMut b);

/// @}

/// @name permutations
/// @{

/// rearranges into the next permutation in 'cmp' order
/// @param s the span
/// @param cmp the order permutations are counted in
/// @return whether there was one; the last wraps to the first and returns false, so a
///         do-while from a sorted span visits each permutation once
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_next_permutation(tda_SpanMut s, tda_Cmp cmp);

/// rearranges into the previous permutation in 'cmp' order
/// @param s the span
/// @param cmp the order permutations are counted in
/// @return whether there was a previous one; on the first it wraps to the last
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_prev_permutation(tda_SpanMut s, tda_Cmp cmp);

/// @}

/// @name shuffle
/// @{

/// rearranges into an order drawn from 'rng'
/// @param s the span
/// @param rng the source of randomness; the same seed gives the same shuffle
/// @bigo{n}. Fisher-Yates: each position draws from the elems not placed yet, so all
///          n! orders are equally likely
TDA_API
void tda_span_shuffle(tda_SpanMut s, tda_Rng *rng);

/// shuffles only far enough to settle the first 'count' positions
/// @param s the span; every elem may move, not just the first 'count'
/// @param count how many positions to settle; asserts count <= s.len
/// @param rng the source of randomness
/// @bigo{count}. Fisher-Yates stopped early: the prefix is a uniform sample of the whole
///              span; the rest holds the remaining elems in no set order
TDA_API
void tda_span_shuffle_prefix(tda_SpanMut s, size_t count, tda_Rng *rng);

/// @}

/// @name partition
/// @{

/// moves the elems satisfying 'pred' to the front
/// @param s the span
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return the index of the first elem that does not satisfy 'pred'
/// @bigo{n}; it swaps, so neither side keeps its order
[[nodiscard]] TDA_API
size_t tda_span_partition(tda_SpanMut s, tda_Pred pred, void *ctx);

/// moves the elems satisfying 'pred' to the front, both sides keeping their order
/// @param s the span
/// @param pred the test, called once per elem
/// @param ctx handed to 'pred'
/// @param al for a span-sized scratch buffer
/// @param[out] out_boundary where the front part ends, written only on success
/// @retval TDA_STATUS_OK on success
/// @retval TDA_STATUS_ERR_NO_MEM when the buffer cannot be allocated; 's' is untouched
/// @bigo{n}
[[nodiscard]] TDA_API
tda_Status tda_span_partition_stable(
    tda_SpanMut s,
    tda_Pred pred,
    void *ctx,
    tda_Al *al,
    size_t *out_boundary
);

/// whether the span is already partitioned by 'pred'
/// @param s the span
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return whether every elem satisfying 'pred' comes before every elem that does not
/// @bigo{n}
[[nodiscard]] TDA_API
bool tda_span_is_partitioned(tda_Span s, tda_Pred pred, void *ctx);

/// @}

/// @}
