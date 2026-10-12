#pragma once

/// @file

/// @defgroup core_export core/export
/// @ingroup core
/// @brief TDA_API, which marks a symbol as part of the public surface
///
/// The shared library is built with hidden visibility, so only symbols marked with this
/// are exported. The rest stays internal and can change without breaking callers.
///
/// Doxygen expands it away, so the reference does not show it.
///
/// @{

// export when building the shared library, import when using it, empty under
// TDA_STATIC. Not a /// comment: the reference never shows this macro
#if defined(TDA_STATIC)
    #define TDA_API
#elif defined(_WIN32) || defined(__CYGWIN__)
#if defined(TDA_BUILD)
    #define TDA_API __declspec(dllexport)
#else
    #define TDA_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4
    #define TDA_API __attribute__((visibility("default")))
#else
    #define TDA_API
#endif

/// @}
