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

/// A log. A value the caller holds, on the stack or inside a struct: tda_log_init
/// fills it in, TDA_AL hands out the allocator over it, and tda_log_deinit
/// writes its last line. `= {}` is the inert state.
///
/// The fields belong to the log: read and change them only through the functions here.
///
/// @warning the log must not move, nor be copied, once it is filled in: the allocator
///          TDA_AL gives points into it.
///          A moved or copied one is caught by the first of its own functions
///          called on it, deinit included, under assert or TDA_HARDENED; a request
///          made through its tda_Al is not checked.
typedef struct tda_Log {
    tda_Al al_;      ///< the interface; its ctx is the log itself
    tda_Al *parent_; ///< the allocator every call goes on to
    FILE *stream_;   ///< where the lines go
} tda_Log;

/// @name lifetime
/// @{

/// a log over 'parent', writing to 'stream'; takes no memory, so it cannot fail
/// @param parent the allocator to pass every call to; borrowed
/// @param stream where the lines go; borrowed, must outlive the log
/// @param[out] out the log
/// @bigo{1}
TDA_API
void tda_log_init(tda_Al *parent, FILE *stream, tda_Log *out);

/// writes the last line and leaves the log inert
/// @param self a log, or an inert one, for which this is a no-op
/// @bigo{1}
TDA_API
void tda_log_deinit(tda_Log *self);

/// @}

/// @}
