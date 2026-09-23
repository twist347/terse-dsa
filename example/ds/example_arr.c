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

// an equality that sees less than the bytes do: two elems equal under it can still
// differ byte for byte
static bool eq_abs_i32(const void *lhs, const void *rhs) {
    const int32_t a = *(const int32_t *) lhs;
    const int32_t b = *(const int32_t *) rhs;

    return (a < 0 ? -a : a) == (b < 0 ? -b : b);
}

int main() {
    /// [build]
    // the handle comes back through 'out', and the status cannot be ignored
    tda_Al *al = tda_al_default();

    tda_Arr *a = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_ARR_OF(int32_t, al, &a, 5, 3, 1, 4, 2))) {
        return 1;
    }
    /// [build]

    /// [compare]
    // two arrs are equal when they hold the same elems: the same length, the same bytes
    tda_Arr *twin = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_ARR_OF(int32_t, al, &twin, 5, 3, 1, 4, 2))) {
        tda_arr_drop(a);
        return 1;
    }
    printf("%d\n", tda_arr_eq(a, twin)); // 1

    // an elem whose equality is not its bytes needs the other form, which asks a tda_Eq
    TDA_ARR_SET(int32_t, twin, 0, -5);
    printf("%d %d\n", tda_arr_eq(a, twin), tda_arr_eq_by(a, twin, eq_abs_i32)); // 0 1

    tda_arr_drop(twin);
    /// [compare]

    /// [algo]
    // an arr has no order of its own to protect, so algo rearranges the elems in place
    tda_span_sort(tda_arr_to_span_mut(a), tda_cmp_i32);

    size_t idx;
    if (tda_span_binary_search(tda_arr_to_span(a), &(int32_t){4}, tda_cmp_i32, &idx)) {
        printf("4 is at %zu\n", idx); // 4 is at 3
    }
    /// [algo]

    /// [access]
    TDA_ARR_SET(int32_t, a, 0, 0);
    printf("%" PRId32 " .. %" PRId32 " over %zu elems\n", *TDA_ARR_FRONT_AS(int32_t, a),
           *TDA_ARR_BACK_AS(int32_t, a), tda_arr_len(a)); // 0 .. 5 over 5 elems
    /// [access]

    /// [copy]
    // every op that allocates can fail, and C has no defer: once a resource is held, the
    // failure path jumps to a common exit instead of returning early
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

    // the other: an arr that already exists is overwritten, and its block is resized to
    // whatever the source needs — here from two elems to five
    if (TDA_STATUS_IS_ERR(TDA_ARR_NEW_LEN(int32_t, 2, al, &shorter))) {
        goto out;
    }
    tda_arr_print(shorter, tda_fprint_i32); // [0, 0] — new_len zeroes the block

    if (TDA_STATUS_IS_ERR(tda_arr_copy_assign(shorter, a))) {
        goto out;
    }
    tda_arr_print(shorter, tda_fprint_i32); // [0, 2, 3, 4, 5]

    // a copy is born where its source lives; copy_with names another allocator instead.
    // The arena bumps a pointer and gives everything back at once, so what it holds must
    // not outlive it
    arena = tda_al_arena_new(al, 1024);
    if (!arena) {
        goto out;
    }

    if (TDA_STATUS_IS_ERR(tda_arr_copy_with(a, arena, &in_arena))) {
        goto out;
    }
    tda_arr_print(in_arena, tda_fprint_i32); // [0, 2, 3, 4, 5]

    // a move hands the elems over and leaves the source empty. These two sit on different
    // allocators, so it costs n and may refuse; on one it would be a handover that cannot
    if (TDA_STATUS_IS_ERR(tda_arr_move_assign(shorter, in_arena))) {
        goto out;
    }
    printf("%zu <- %zu\n", tda_arr_len(shorter), tda_arr_len(in_arena)); // 5 <- 0

    // and the one operation that moves a block: the two are exchanged whole, lengths and
    // all, so a pointer into either of them now points into the other. It wants both on
    // one allocator — 'a' and 'shorter' are on 'al' — since it exchanges the blocks where
    // they lie instead of copying anything, and so has nothing to report
    tda_arr_swap(a, shorter);

    rc = 0;
out:
    // a null handle is a no-op, so this exit is safe from anywhere above. What the arena
    // gave out goes back before the arena itself
    tda_arr_drop(in_arena);
    tda_al_arena_drop(arena);

    tda_arr_drop(shorter);
    tda_arr_drop(copy);
    tda_arr_drop(a);
    return rc;
    /// [copy]
}
