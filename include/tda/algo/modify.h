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
/// The ops that take a key pass it to 'eq' on the right, the elem on the left, so the
/// key need not be an elem.
///
/// @par Example
/// @snippet algo/example_modify.c modify
/// @{

/// @name unique
/// @{

/// drops every elem equal to the last one kept before it, so each run keeps only its
/// first. This is std::unique's rule; with an 'eq' that is not transitive, an elem
/// equal to its neighbour but not to the one kept stays
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
/// @param key asserts it does not point into 's': elems move during the pass, and 'key'
///            would then read whatever lands there
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
/// @param key asserts it does not point into 's': the first match would overwrite it
/// @param val the value to write
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @bigo{n}
TDA_API
void tda_span_replace(tda_SpanMut s, const void *key, const void *val, tda_Eq eq, void *ctx);

/// overwrites every elem satisfying 'pred' with 'val'
/// @param s the span
/// @param val the value to write
/// @param pred the test
/// @param ctx handed to 'pred'
/// @bigo{n}
TDA_API
void tda_span_replace_if(tda_SpanMut s, const void *val, tda_Pred pred, void *ctx);

/// @}

/// @name macros
/// @{

/// tda_span_remove from a value rather than an address
/// @param K the key's type, which need not be the elem's, so nothing checks it against
///          the span; 'key' may be any K, a struct literal in parentheses too
/// @param s the span
/// @param key the value to drop
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @bigo{n}
#define TDA_SPAN_REMOVE(K, s, key, eq, ctx) \
    tda_span_remove((s), &(typeof(K)[1]){ (key) }[0], (eq), (ctx))

/// tda_span_replace from values rather than addresses
/// @param K the key's type, which need not be the elem's, so nothing checks it against
///          the span; 'key' may be any K, a struct literal in parentheses too
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param s the span
/// @param key the value to look for
/// @param val the value to write
/// @param eq the equality
/// @param ctx passed to every call of 'eq'; may be null
/// @bigo{n}
#define TDA_SPAN_REPLACE(K, T, s, key, val, eq, ctx) \
    tda_span_replace(                                \
        tda_span_mut_typed_((s), TDA_LAYOUT_OF(T)),  \
        &(typeof(K)[1]){ (key) }[0],                 \
        &(typeof(T)[1]){ (val) }[0],                 \
        (eq),                                        \
        (ctx)                                        \
    )

/// tda_span_replace_if from a value rather than an address
/// @param T the elem type; 'val' may be any T, a struct literal in parentheses too
/// @param s the span
/// @param val the value to write
/// @param pred the test
/// @param ctx handed to 'pred'
/// @bigo{n}
#define TDA_SPAN_REPLACE_IF(T, s, val, pred, ctx)   \
    tda_span_replace_if(                            \
        tda_span_mut_typed_((s), TDA_LAYOUT_OF(T)), \
        &(typeof(T)[1]){ (val) }[0],                \
        (pred),                                     \
        (ctx)                                       \
    )

/// @}

/// @}
