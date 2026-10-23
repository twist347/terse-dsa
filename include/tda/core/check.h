#pragma once

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/// @file

/// @defgroup core_check core/check
/// @ingroup core
/// @brief asserts that survive NDEBUG: TDA_CHECK always, TDA_EXPECT under TDA_HARDENED
///
/// For preconditions whose breach would be UB in release too. Each costs a branch, so
/// use them for cheap checks only.
///
/// @par Example
/// @snippet core/example_check.c check
/// @snippet core/example_check.c hardened
/// @{

/// @name macro
/// @{

/// aborts with the condition's text and location if it fails, in every build
/// @param ... the condition; variadic like C23 assert, so commas inside are fine
#define TDA_CHECK(...)                                                     \
    ((__VA_ARGS__) ? (void) 0                                              \
                   : (fprintf(stderr, "%s:%d: %s: check failed: %s\n",     \
                              __FILE__, __LINE__, __func__, #__VA_ARGS__), \
                      abort()))

#ifdef TDA_HARDENED
    #define TDA_EXPECT(...) TDA_CHECK(__VA_ARGS__)
#else
    /// a precondition whose breach is out-of-bounds memory: TDA_CHECK under
    /// TDA_HARDENED, assert otherwise
    /// @param ... the condition, as in TDA_CHECK
    #define TDA_EXPECT(...) assert(__VA_ARGS__)
#endif

/// @}

/// @}
