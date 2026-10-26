#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/export.h"

#include <stdio.h>

/// @file

/// @defgroup alloc_log alloc/log
/// @ingroup alloc
/// @brief an allocator that writes down what another one is asked to do
///
/// Every call is printed and passed to the parent allocator unchanged. Put it under a
/// container to see when the container grows.
///
/// The log takes no memory of its own; the parent allocator must outlive it.
///
/// @par Example
/// @snippet alloc/example_log.c wrap
/// @{

/// A log. A value the caller holds, on the stack or inside a struct: tda_log_init makes
/// it, tda_log_to_al hands out views of it, and tda_log_deinit writes its last line.
/// `= {}` is the inert state.
///
/// The fields belong to the log: read and change them only through the functions here.
///
/// @warning once a view from tda_log_to_al is out, the log must not move nor be copied:
///          the view points at it, and so does every container built on it.
typedef struct tda_Log {
    tda_Al parent_; ///< the allocator every call goes on to
    FILE *stream_;  ///< where the lines go
} tda_Log;

/// @name lifetime
/// @{

/// a log over 'parent', writing to 'stream'; takes no memory, so it cannot fail
/// @param stream where the lines go; borrowed, must outlive the log
/// @param parent the allocator to pass every call to; borrowed
/// @return the log
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Log tda_log_init(FILE *stream, tda_Al parent);

/// writes the last line and leaves the log inert
/// @param self a log, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_log_deinit(tda_Log *self);

/// @}

/// @name view
/// @{

/// a view of the log, to hand to a container or a wrapper
/// @param self the log; from now on it must stay where it is
/// @return the view, valid while the log stays where it is and until its deinit
/// @bigo{1}
[[nodiscard]] TDA_API
tda_Al tda_log_to_al(tda_Log *self);

/// @}

/// @}
