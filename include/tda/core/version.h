#pragma once

#include "tda/core/util.h"

/// @file

/// @defgroup core_version core/version
/// @ingroup core
/// @brief the version of these headers
///
/// The one place the version is written: CMakeLists.txt reads it from here.
/// @{

/// @name macro
/// @{

/// bumped by a change that breaks a caller
#define TDA_VERSION_MAJOR 1

/// bumped by an addition
#define TDA_VERSION_MINOR 0

/// bumped by a fix
#define TDA_VERSION_PATCH 0

/// the version as one comparable number
/// @param major the major part
/// @param minor the minor part, below 1000
/// @param patch the patch part, below 1000
#define TDA_VERSION_NUM(major, minor, patch) ((major) * 1000000 + (minor) * 1000 + (patch))

/// these headers as TDA_VERSION_NUM
#define TDA_VERSION TDA_VERSION_NUM(TDA_VERSION_MAJOR, TDA_VERSION_MINOR, TDA_VERSION_PATCH)

/// whether these headers are at least the given version; usable by the preprocessor
/// @param major the major part
/// @param minor the minor part
/// @param patch the patch part
#define TDA_VERSION_AT_LEAST(major, minor, patch) (TDA_VERSION >= TDA_VERSION_NUM(major, minor, patch))

/// the version as a string literal, "1.0.0"
#define TDA_VERSION_STRING                  \
    TDA_STRINGIFY(TDA_VERSION_MAJOR) "."    \
    TDA_STRINGIFY(TDA_VERSION_MINOR) "."    \
    TDA_STRINGIFY(TDA_VERSION_PATCH)

/// @}

/// @}
