#pragma once

#include "tda/algo/fn.h"
#include "tda/core/cmp.h"
#include "tda/core/export.h"
#include "tda/core/span.h"

#include <stddef.h>

/// @file

/// @defgroup algo_modify algo/modify
/// @ingroup algo
/// @brief changing what a span holds, in place
///
/// A span cannot resize itself, so the ops that drop elems return the new length instead:
/// the kept elems are packed to the front, and everything from there to s.len is left in
/// an unspecified state. The caller shortens its own container:
///
///     const size_t kept = tda_span_unique(tda_vec_to_span_mut(v), tda_eq_i32);
///     tda_Status st = tda_vec_resize(v, kept);
///
/// [[nodiscard]] makes dropping that length a warning the compiler raises unasked, and an
/// error under -Werror.
///
/// @par Example
/// @snippet algo/example_modify.c drop
/// @{

/// @name unique
/// @{

/// drops every elem equal to the one before it, so a run collapses to its first
/// @param s the span
/// @param eq the equality
/// @return the new length. Only neighbours are compared, so this leaves a set over a
///         sorted span and merely collapses runs over any other
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_unique(tda_SpanMut s, tda_Eq eq);

/// @}

/// @name remove
/// @{

/// drops every elem equal to 'key'
/// @param s the span
/// @param key the address of the value to drop; must not point into 's': the elems move
///            under it, and it would be compared as whatever lands there next
/// @param eq the equality
/// @return the new length
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_remove(tda_SpanMut s, const void *key, tda_Eq eq);

/// drops every elem satisfying 'pred'
/// @param s the span
/// @param pred the test
/// @param ctx handed to 'pred'
/// @return the new length
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_remove_if(tda_SpanMut s, tda_Pred pred, void *ctx);

/// @}

/// @name replace
/// @{

/// overwrites every elem equal to 'key' with 'val'
/// @param s the span
/// @param key the address of the value to look for; must not point into 's', as in
///            tda_span_remove — the first match overwrites it
/// @param val the address of the value to write; the length never changes, so there is
///            nothing to return
/// @param eq the equality
/// @bigo{n}
TDA_API
void tda_span_replace(tda_SpanMut s, const void *key, const void *val, tda_Eq eq);

/// overwrites every elem satisfying 'pred' with 'val'
/// @param s the span
/// @param pred the test
/// @param ctx handed to 'pred'
/// @param val the address of the value to write
/// @bigo{n}
TDA_API
void tda_span_replace_if(tda_SpanMut s, tda_Pred pred, void *ctx, const void *val);

/// @}

/// @}
