#pragma once

#include "tda/alloc/alloc.h"

/// @file

/// @defgroup alloc_default alloc/default
/// @ingroup alloc
/// @brief the allocator that is just malloc
///
/// The default choice, and the usual parent of an arena or a pool.
///
/// @{

/// @name lifetime
/// @{

/// the default allocator
/// @return a view of it, the same on every call; it has no state, lives for the whole
///         program and has no deinit
/// @note the one allocator here that threads may share: with no state of its own, each
///       hook is a call into libc, which is thread-safe
/// @bigo{1}; its hooks cost what malloc, calloc, realloc and free cost
[[nodiscard]] TDA_API
tda_Al tda_al_default();

/// @}

/// @}
