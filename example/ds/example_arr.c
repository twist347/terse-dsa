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
    const int32_t a = *(const int32_t *) lhs, b = *(const int32_t *) rhs;

    return (a < 0 ? -a : a) == (b < 0 ? -b : b);
}

int main() {
    /// [build]
    // the arr is filled in through 'out', and the status must be checked
    tda_Al *al = tda_al_default();

    tda_Arr a;
    if (TDA_STATUS_IS_ERR(TDA_ARR_OF(int32_t, al, &a, 5, 3, 1, 4, 2))) {
        return 1;
    }
    /// [build]

    /// [compare]
    // two arrs are equal when they have the same length and the same bytes
    tda_Arr twin;
    if (TDA_STATUS_IS_ERR(TDA_ARR_OF(int32_t, al, &twin, 5, 3, 1, 4, 2))) {
        tda_arr_deinit(&a);
        return 1;
    }
    printf("%d\n", tda_arr_eq(&a, &twin)); // 1

    // an elem whose equality is not bytewise needs eq_by, which takes a tda_Eq
    TDA_ARR_SET(int32_t, &twin, 0, -5);
    printf("%d %d\n", tda_arr_eq(&a, &twin), tda_arr_eq_by(&a, &twin, eq_abs_i32, nullptr)); // 0 1

    tda_arr_deinit(&twin);
    /// [compare]

    /// [algo]
    // an arr keeps no order of its own, so algo rearranges the elems in place
    tda_span_sort(tda_arr_to_span_mut(&a), tda_cmp_i32, nullptr);

    size_t idx;
    if (tda_span_binary_search(tda_arr_to_span(&a), &(int32_t){4}, tda_cmp_i32, nullptr, &idx)) {
        printf("4 is at %zu\n", idx); // 4 is at 3
    }
    /// [algo]

    /// [access]
    TDA_ARR_SET(int32_t, &a, 0, 0);
    printf(
        "%" PRId32 " .. %" PRId32 " over %zu elems\n",
        *TDA_ARR_FRONT_AS(int32_t, &a),
        *TDA_ARR_BACK_AS(int32_t, &a),
        tda_arr_len(&a)
    ); // 0 .. 5 over 5 elems
    /// [access]

    /// [copy]
    // every allocating op can fail and C has no defer: once a resource is held, failures
    // jump to a common exit instead of returning early
    tda_Arr copy = {}, shorter = {}, in_arena = {};
    tda_Arena arena = {};
    int rc = 1;

    // one way to copy: a fresh arr with the same elems, on the same allocator as 'a'
    if (TDA_STATUS_IS_ERR(tda_arr_copy(&a, &copy))) {
        goto out;
    }
    tda_arr_print(&copy, tda_fprint_i32); // [0, 2, 3, 4, 5]

    // the other: an existing arr is overwritten and its block resized to fit the source,
    // here from two elems to five
    if (TDA_STATUS_IS_ERR(TDA_ARR_INIT_LEN(int32_t, 2, al, &shorter))) {
        goto out;
    }
    tda_arr_print(&shorter, tda_fprint_i32); // [0, 0] — init_len zeroes the block

    if (TDA_STATUS_IS_ERR(tda_arr_copy_assign(&shorter, &a))) {
        goto out;
    }
    tda_arr_print(&shorter, tda_fprint_i32); // [0, 2, 3, 4, 5]

    // a copy uses its source's allocator; copy_with takes another one. The arena bumps a
    // pointer and frees everything at once, so what it holds must not outlive it
    if (TDA_STATUS_IS_ERR(tda_arena_init(al, 1024, &arena))) {
        goto out;
    }

    if (TDA_STATUS_IS_ERR(tda_arr_copy_with(&a, TDA_AL(&arena), &in_arena))) {
        goto out;
    }
    tda_arr_print(&in_arena, tda_fprint_i32); // [0, 2, 3, 4, 5]

    // a move hands the elems over with the allocator they came from and empties the
    // source; nothing is copied, so it cannot fail, whatever the two allocators are
    tda_arr_move_assign(&shorter, &in_arena);
    printf("%zu <- %zu\n", tda_arr_len(&shorter), tda_arr_len(&in_arena)); // 5 <- 0

    // swap exchanges the two arrs whole, lengths and allocators included: before it
    // 'shorter' holds the arena's block, after it 'a' does. Each block goes along with its
    // allocator, nothing is copied, and there is nothing to report
    tda_arr_swap(&a, &shorter);
    tda_arr_print(&a, tda_fprint_i32);       // [0, 2, 3, 4, 5], in the arena
    tda_arr_print(&shorter, tda_fprint_i32); // [0, 2, 3, 4, 5], on the default allocator

    rc = 0;
out:
    // deinit of an inert value is a no-op, so this exit is safe from anywhere above.
    // Whatever holds the arena's memory goes before the arena itself
    tda_arr_deinit(&in_arena);
    tda_arr_deinit(&shorter);
    tda_arr_deinit(&copy);
    tda_arr_deinit(&a);
    tda_arena_deinit(&arena);

    return rc;
    /// [copy]
}
