#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/core/status.h"
#include "tda/ds/hmap.h"

#include <stddef.h>

/*
 * The internal constructor shared by hmap and hset. Only it accepts a zero-sized value,
 * since C has no unit type and public maps must return real value pointers. A set's node
 * then holds just the header and key, padded to its alignment.
 */

/// like tda_hmap_init_cap, but 'val' may have size 0. Not TDA_API: the library's default
/// hidden visibility keeps it out of the shared object's symbol table
[[nodiscard]]
tda_Status tda_hmap_init_raw_(
    size_t cap,
    tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al al,
    tda_HMap *out
);
