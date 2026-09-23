#pragma once

#include <stdio.h>
#include <stdlib.h>

/// @file

/// @defgroup core_check core/check
/// @ingroup core
/// @brief TDA_CHECK — an assert that survives NDEBUG
///
/// For preconditions whose breach would be UB in release too. Costs a branch, so cheap
/// checks only.
/// @{

/// @name macro
/// @{

/// aborts with the condition's text and place when it does not hold, in every build
/// @param ... the condition; variadic, as C23 assert, so commas inside it are fine
#define TDA_CHECK(...)                                                     \
    ((__VA_ARGS__) ? (void) 0                                              \
                   : (fprintf(stderr, "%s:%d: %s: check failed: %s\n",     \
                              __FILE__, __LINE__, __func__, #__VA_ARGS__), \
                      abort()))

/// @}

/// @}
