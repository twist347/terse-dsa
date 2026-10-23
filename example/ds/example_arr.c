// for @snippet

#include "tda/algo/search.h"
#include "tda/algo/sort.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/ds/arr.h"

#include <inttypes.h>
#include <stdio.h>

// an equality looser than bytewise: elems equal under it may differ in their bytes
static bool eq_abs_i32(const void *lhs, const void *rhs, void *) {
    const int32_t a = *(const int32_t *) lhs;
    const int32_t b = *(const int32_t *) rhs;

    return (a < 0 ? -a : a) == (b < 0 ? -b : b);
}

int main() {
    /// [build]
    // the handle comes back through 'out', and the status must be checked
    tda_Al *al = tda_al_default();

    tda_Arr *a = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_ARR_OF(int32_t, al, &a, 5, 3, 1, 4, 2))) {
        return 1;
    }
    /// [build]

    /// [compare]
    // two arrs are equal when they have the same length and the same bytes
    tda_Arr *twin = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_ARR_OF(int32_t, al, &twin, 5, 3, 1, 4, 2))) {
        tda_arr_drop(a);
        return 1;
    }
    printf("%d\n", tda_arr_eq(a, twin)); // 1

    // an elem whose equality is not bytewise needs eq_by, which takes a tda_Eq
    TDA_ARR_SET(int32_t, twin, 0, -5);
    printf("%d %d\n", tda_arr_eq(a, twin), tda_arr_eq_by(a, twin, eq_abs_i32, nullptr)); // 0 1

    tda_arr_drop(twin);
    /// [compare]

    /// [algo]
    // an arr keeps no order of its own, so algo rearranges the elems in place
    tda_span_sort(tda_arr_to_span_mut(a), tda_cmp_i32, nullptr);

    size_t idx;
    if (tda_span_binary_search(tda_arr_to_span(a), &(int32_t){4}, tda_cmp_i32, nullptr, &idx)) {
        printf("4 is at %zu\n", idx); // 4 is at 3
    }
    /// [algo]

    /// [access]
    TDA_ARR_SET(int32_t, a, 0, 0);
    printf("%" PRId32 " .. %" PRId32 " over %zu elems\n", *TDA_ARR_FRONT_AS(int32_t, a),
           *TDA_ARR_BACK_AS(int32_t, a), tda_arr_len(a)); // 0 .. 5 over 5 elems
    /// [access]

    /// [copy]
    // every allocating op can fail and C has no defer: once a resource is held, failures
    // jump to a common exit instead of returning early
    tda_Arr *copy = nullptr;
    tda_Arr *shorter = nullptr;
    tda_Arr *in_arena = nullptr;
    tda_Al *arena = nullptr;
    int rc = 1;

    // one way to copy: a fresh arr with the same elems, on the same allocator as 'a'
    if (TDA_STATUS_IS_ERR(tda_arr_copy(a, &copy))) {
        goto out;
    }
    tda_arr_print(copy, tda_fprint_i32); // [0, 2, 3, 4, 5]

    // the other: an existing arr is overwritten and its block resized to fit the source,
    // here from two elems to five
    if (TDA_STATUS_IS_ERR(TDA_ARR_NEW_LEN(int32_t, 2, al, &shorter))) {
        goto out;
    }
    tda_arr_print(shorter, tda_fprint_i32); // [0, 0] — new_len zeroes the block

    if (TDA_STATUS_IS_ERR(tda_arr_copy_assign(shorter, a))) {
        goto out;
    }
    tda_arr_print(shorter, tda_fprint_i32); // [0, 2, 3, 4, 5]

    // a copy uses its source's allocator; copy_with takes another one. The arena bumps a
    // pointer and frees everything at once, so what it holds must not outlive it
    arena = tda_al_arena_new(al, 1024);
    if (!arena) {
        goto out;
    }

    if (TDA_STATUS_IS_ERR(tda_arr_copy_with(a, arena, &in_arena))) {
        goto out;
    }
    tda_arr_print(in_arena, tda_fprint_i32); // [0, 2, 3, 4, 5]

    // a move hands the elems over and empties the source. Across two allocators, as here,
    // it costs n and can fail; on one it is a handover that cannot fail
    if (TDA_STATUS_IS_ERR(tda_arr_move_assign(shorter, in_arena))) {
        goto out;
    }
    printf("%zu <- %zu\n", tda_arr_len(shorter), tda_arr_len(in_arena)); // 5 <- 0

    // swap exchanges the two arrs whole, lengths included, so a pointer into one now
    // points into the other. Both must be on one allocator ('a' and 'shorter' are on
    // 'al'): the blocks trade places without copying, so there is nothing to report
    tda_arr_swap(a, shorter);

    rc = 0;
out:
    // a null handle is a no-op, so this exit is safe from anywhere above. The arena's
    // allocations go back before the arena itself
    tda_arr_drop(in_arena);
    tda_al_arena_drop(arena);

    tda_arr_drop(shorter);
    tda_arr_drop(copy);
    tda_arr_drop(a);

    return rc;
    /// [copy]
}
