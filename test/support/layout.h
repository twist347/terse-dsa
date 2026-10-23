#pragma once

#include "tda/core/layout.h"

#include <stddef.h>

/* ========== layouts ========== */

/// a layout of 'size' bytes at 'align', for the tests that ask an allocator for raw
/// blocks rather than for a type's
[[nodiscard]]
static inline tda_Layout tda_test_layout(size_t size, size_t align) {
    return (tda_Layout){.size = size, .align = align};
}
