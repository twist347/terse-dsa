// for @snippet

#include "tda/algo/modify.h"
#include "tda/algo/sort.h"
#include "tda/alloc/arena.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/ds/vec.h"

#include <inttypes.h>
#include <stdio.h>

// one vec on the default allocator, one in an arena
static int show_allocators() {
    tda_Al al = tda_al_default();

    tda_Arena arena;
    if (TDA_STATUS_IS_ERR(tda_arena_init_cap(1024, al, &arena))) {
        return 1;
    }

    /// [allocators]
    // a vec carries its allocator, so two on different allocators swap in O(1) and each
    // block still goes back to the allocator that made it
    tda_Vec a = TDA_VEC_INIT(int32_t, al), b = TDA_VEC_INIT(int32_t, tda_arena_to_al(&arena));
    int rc = 1;

    if (TDA_STATUS_IS_ERR(TDA_VEC_EXTEND(int32_t, &a, 1, 2, 3))) {
        goto out;
    }
    if (TDA_STATUS_IS_ERR(TDA_VEC_EXTEND(int32_t, &b, 7, 8))) {
        goto out;
    }

    tda_vec_swap(&a, &b);
    tda_vec_print(&a, tda_fprint_i32, nullptr); // [7, 8]
    tda_vec_print(&b, tda_fprint_i32, nullptr); // [1, 2, 3]

    rc = 0;
out:
    tda_vec_deinit(&a);
    tda_vec_deinit(&b);
    /// [allocators]
    tda_arena_deinit(&arena);

    return rc;
}

// a vec filled in a loop that knows its size
static int show_reserved() {
    /// [reserved]
    // a loop that knows its size reserves once; the pushes inside then cannot fail, so
    // they need no status, and add_one hands out a slot to fill in place
    tda_Vec v = TDA_VEC_INIT(int32_t, tda_al_default());
    if (TDA_STATUS_IS_ERR(tda_vec_reserve(&v, 4))) {
        return 1;
    }

    for (int32_t i = 1; i <= 3; ++i) {
        TDA_VEC_PUSH_ASSUME_CAP(int32_t, &v, i * i);
    }
    *TDA_VEC_ADD_ONE_ASSUME_CAP_AS(int32_t, &v) = 16;

    tda_vec_print(&v, tda_fprint_i32, nullptr); // [1, 4, 9, 16]
    tda_vec_deinit(&v);
    /// [reserved]

    return 0;
}

int main() {
    if (show_allocators() != 0 || show_reserved() != 0) {
        return 1;
    }

    /// [build]
    // the vec is a value the caller holds; one built with elems comes back through
    // 'out', and the status must be checked
    tda_Al al = tda_al_default();

    tda_Vec v;
    if (TDA_STATUS_IS_ERR(TDA_VEC_OF(int32_t, al, &v, 5, 3, 1))) {
        return 1;
    }

    printf("%zu of %zu\n", tda_vec_len(&v), tda_vec_cap(&v)); // 3 of 3
    /// [build]

    /// [grow]
    // the block doubles when full, so pushes cost O(1) amortized. Every op that may
    // reallocate returns a status, and C has no defer: once the vec is held, failures
    // jump to a common exit instead of returning early
    int rc = 1;

    if (TDA_STATUS_IS_ERR(TDA_VEC_PUSH(int32_t, &v, 4))) {
        goto out;
    }
    printf("%zu of %zu\n", tda_vec_len(&v), tda_vec_cap(&v)); // 4 of 6

    // pop shortens the length and keeps the capacity
    tda_vec_pop(&v);
    printf("%zu of %zu\n", tda_vec_len(&v), tda_vec_cap(&v)); // 3 of 6

    // shrink_to_fit gives the room back, and can fail like any resize
    if (TDA_STATUS_IS_ERR(tda_vec_shrink_to_fit(&v))) {
        goto out;
    }
    printf("%zu of %zu\n", tda_vec_len(&v), tda_vec_cap(&v)); // 3 of 3
    /// [grow]

    /// [bulk]
    // extend reserves once for the whole run instead of per elem, and insert_span moves
    // the tail once instead of once per elem
    if (TDA_STATUS_IS_ERR(TDA_VEC_EXTEND(int32_t, &v, 4, 2))) {
        goto out;
    }
    tda_vec_print(&v, tda_fprint_i32, nullptr); // [5, 3, 1, 4, 2]

    if (TDA_STATUS_IS_ERR(TDA_VEC_INSERT_SPAN(int32_t, &v, 1, 9, 9))) {
        goto out;
    }
    tda_vec_print(&v, tda_fprint_i32, nullptr); // [5, 9, 9, 3, 1, 4, 2]

    // and the way back out, which allocates nothing and so cannot fail
    tda_vec_remove_range(&v, 1, 2);
    tda_vec_print(&v, tda_fprint_i32, nullptr); // [5, 3, 1, 4, 2]
    /// [bulk]

    /// [algo]
    // the bridge to algo works both ways. Out: a writable view; algo rearranges the elems
    // in place, since a vec has no order to protect
    tda_span_sort(tda_vec_to_span_mut(&v), tda_cmp_i32, nullptr);
    tda_vec_print(&v, tda_fprint_i32, nullptr); // [1, 2, 3, 4, 5]

    // back: an algorithm that drops elems packs the kept ones to the front and returns
    // the count, and the vec adopts that length
    const size_t kept = tda_span_remove(tda_vec_to_span_mut(&v), &(int32_t){3}, tda_eq_i32, nullptr);
    tda_vec_truncate(&v, kept);
    tda_vec_print(&v, tda_fprint_i32, nullptr); // [1, 2, 4, 5]

    printf(
        "%" PRId32 " .. %" PRId32 "\n",
        *TDA_VEC_FRONT_AS(int32_t, &v),
        *TDA_VEC_BACK_AS(int32_t, &v)
    ); // 1 .. 5

    rc = 0;
out:
    // deinit of an inert vec is a no-op, so this exit is safe from anywhere above
    tda_vec_deinit(&v);

    return rc;
    /// [algo]
}
