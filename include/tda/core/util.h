#pragma once

/// @file

/// @defgroup core_util core/util
/// @ingroup core
/// @brief macros that exist because a function cannot do the job
///
/// A macro belongs here only when a function cannot replace it: it works on the type
/// through typeof, keeps lvalue semantics, or needs the tokens rather than the value.
///
/// @par Example
/// @snippet core/example_util.c swap
/// @snippet core/example_util.c stringify
/// @snippet core/example_util.c unused
/// @{

/// @name macro
/// @{

/// the inner step of TDA_STRINGIFY; the extra level makes the argument expand first
/// @param x the tokens to spell
#define TDA_STRINGIFY_(x)   #x

/// the argument as a string literal, expanded first: TDA_STRINGIFY(TDA_STATUS_OK) is
/// "TDA_STATUS_OK", not "st"
/// @param x the tokens to spell
#define TDA_STRINGIFY(x)    TDA_STRINGIFY_(x)

/// evaluates 'val' and discards it, to mark a parameter or result as unused on purpose
/// @param val what to evaluate and discard
#define TDA_UNUSED(val)    ((void) (val))

/// exchanges two lvalues of one type
/// @param a one lvalue; both are typed from it, so a mismatch is a compile error, not a
///          silent truncation
/// @param b the other lvalue
/// @warning TDA_SWAP(self, other) and TDA_SWAP(*self, *other) both typecheck; the first
///          swaps the local pointers and leaves the objects alone
#define TDA_SWAP(a, b)                                        \
    do {                                                      \
        typeof_unqual(a) *tda_swap_pa_ = &(a);                \
        typeof_unqual(a) *tda_swap_pb_ = &(b);                \
        const typeof_unqual(a) tda_swap_tmp_ = *tda_swap_pa_; \
        *tda_swap_pa_ = *tda_swap_pb_;                        \
        *tda_swap_pb_ = tda_swap_tmp_;                        \
    } while (0)

/// @}

/// @}
