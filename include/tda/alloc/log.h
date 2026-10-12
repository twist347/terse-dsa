#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"

#include <stdio.h>

/// @file

/// @defgroup alloc_log alloc/log
/// @ingroup alloc
/// @brief an allocator that writes down what another one is asked to do
///
/// Every call is printed and passed to the wrapped allocator unchanged. Put it under a
/// container to see when the container grows.
///
/// Its own two blocks come from the wrapped allocator and go back to it on drop, so the
/// wrapped allocator must outlive the log.
///
/// @par Example
/// @snippet alloc/example_log.c wrap
/// @{

/// @name lifetime
/// @{

/// a log over 'wrapped', writing to 'stream'
/// @param wrapped the allocator to pass every call to; borrowed
/// @param stream where the lines go; borrowed, must outlive the log
/// @return the allocator, or null if 'wrapped' could not give it its own two blocks
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al *tda_al_log_new(tda_Al *wrapped, FILE *stream);

/// gives its own two blocks back to the wrapped allocator
/// @param self the log; null is a no-op
/// @bigo{1}
TDA_API
void tda_al_log_drop(tda_Al *self);

/// @}

/// @}
