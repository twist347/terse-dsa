#pragma once

#include "tda/core/export.h"

#include <stdint.h>

/// @file

/// @defgroup core_status core/status
/// @ingroup core
/// @brief tda_Status, how an operation reports failure
///
/// A fallible op returns a tda_Status and writes its result through a trailing 'out',
/// touched only on TDA_STATUS_OK. The return is [[nodiscard]]. C has no defer, so errors
/// are propagated by hand: a return when nothing is held yet, a goto when something is.
///
/// The inverse shape (value returned, status written out) is deliberately absent, since
/// a status out-param is easy to ignore. The alloc wrappers are the exception: they
/// report failure with a null pointer.
///
/// A broken precondition asserts instead: a null self, an index past the end, an
/// elem_size of 0. A tda_Status is for what valid code can still hit at runtime, which
/// is running out of memory; there is no enumerator for a bad argument.
///
/// @par Example
/// @snippet core/example_status.c propagate
/// @snippet core/example_status.c report
/// @{

/// How an operation reports its outcome. Fixed at int32_t so its width across an ABI
/// boundary does not depend on the compiler.
typedef enum : int32_t {
    TDA_STATUS_OK = 0,     ///< success; the only value on which 'out' is written
    TDA_STATUS_ERR_NO_MEM, ///< an allocation failed, or the size asked for overflowed
} tda_Status;

/// @name to str
/// @{

/// the status spelled as in this header
/// @param st the status
/// @return a static string such as "TDA_STATUS_OK", or "UNKNOWN_TDA_STATUS" for a value
///         outside the enum; never null, not to be freed
/// @bigo{1}
[[nodiscard]] TDA_API
const char *tda_status_to_str(tda_Status st);

/// @}

/// @name macros
/// @{

/// whether the op succeeded
/// @param st the status, evaluated once
/// @bigo{1}
#define TDA_STATUS_IS_OK(st)    ((st) == TDA_STATUS_OK)

/// whether the op failed; the form the library's call sites use, since they act on
/// failure and fall through on success
/// @param st the status, evaluated once
/// @bigo{1}
#define TDA_STATUS_IS_ERR(st)   ((st) != TDA_STATUS_OK)

/// @}

/// @}
