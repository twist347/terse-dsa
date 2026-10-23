#pragma once

#include "tda/core/cmp.h"

/*
 * The tie rule of everything that keeps equal elems in their order: the merges in
 * algo/merge and ds/list, insertion sort, and the check for sortedness that their output
 * must pass. A right elem goes before a left one only when it is strictly less, so equal
 * elems stay as they were. Asking only "less" also suits a comparator that never answers
 * "greater", such as one carried over from a C++ less-than.
 */

/// whether 'right' goes before 'left', the elem that came first
[[nodiscard]]
static inline bool tda_right_goes_first(tda_Cmp cmp, void *ctx, const void *left, const void *right) {
    return cmp(right, left, ctx) < 0;
}
