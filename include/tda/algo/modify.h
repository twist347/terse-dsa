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
/// A span cannot resize itself, so the ops that drop elems return the new length. The
/// kept elems are packed to the front and the rest, up to s.len, is left unspecified.
/// The caller shortens its own container:
///
///     const size_t kept = tda_span_unique(tda_vec_to_span_mut(v), tda_eq_i32, nullptr);
///     tda_vec_truncate(v, kept);
///
/// [[nodiscard]] makes ignoring that length a warning (an error under -Werror).
///
/// @par Example
/// @snippet algo/example_modify.c modify
/// @{

/// @name unique
/// @{

/// drops every elem equal to the one before it, so each run keeps only its first
/// @param s the span
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @return the new length. Only neighbours are compared, so only a sorted span ends up
///         free of duplicates
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_unique(tda_SpanMut s, tda_Eq eq, void *ctx);

/// @}

/// @name remove
/// @{

/// drops every elem equal to 'key'
/// @param s the span
/// @param key must not point into 's': elems move during the pass, and 'key' would
///            then read whatever lands there
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @return the new length
/// @bigo{n}
[[nodiscard]] TDA_API
size_t tda_span_remove(tda_SpanMut s, const void *key, tda_Eq eq, void *ctx);

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
/// @param key must not point into 's': the first match would overwrite it
/// @param val the value to write
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @bigo{n}
TDA_API
void tda_span_replace(tda_SpanMut s, const void *key, const void *val, tda_Eq eq, void *ctx);

/// overwrites every elem satisfying 'pred' with 'val'
/// @param s the span
/// @param pred the test
/// @param ctx handed to 'pred'
/// @param val the value to write
/// @bigo{n}
TDA_API
void tda_span_replace_if(tda_SpanMut s, tda_Pred pred, void *ctx, const void *val);

/// @}

/// @}
