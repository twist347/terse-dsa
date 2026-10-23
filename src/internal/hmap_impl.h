#pragma once

#include "tda/alloc/alloc.h"
#include "tda/core/cmp.h"
#include "tda/core/hash.h"
#include "tda/core/status.h"
#include "tda/ds/hmap.h"

#include <stddef.h>

/*
 * The one door into ds/hmap that ds/hset needs and callers must not have.
 *
 * A set is a map whose values carry no information — Rust spells it HashMap<T, ()>. C has
 * no zero-sized type, so the zero has to be allowed somewhere, and here is the somewhere:
 * a value layout of size 0 may come through this constructor and only through it. The public
 * tda_hmap_init_cap still refuses it, so no caller can build a map whose get returns a
 * pointer to nothing.
 *
 * What the zero buys is the whole reason it exists: with no value to follow the key there
 * is nothing to lay out after it, so the node is its struct plus the key, padded only to
 * its own alignment.
 */

/// like tda_hmap_init_cap, but 'val' may have size 0. Not TDA_API: the library's default
/// hidden visibility keeps it out of the shared object's symbol table
[[nodiscard]]
tda_Status tda_hmap_init_raw_(
    size_t cap, tda_Layout key, tda_Layout val,
    tda_Hasher hasher, tda_Eq eq, void *ctx,
    tda_Al *al,
    tda_HMap *out
);
