// for @snippet

#include "tda/algo/search.h"
#include "tda/algo/sort.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/ds/deque.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    // the elems land front to back, in a ring that starts out unwrapped
    tda_Al *al = tda_al_default();

    tda_Deque d;
    if (TDA_STATUS_IS_ERR(TDA_DEQUE_OF(int32_t, al, &d, 3, 4, 5))) {
        return 1;
    }

    tda_deque_print(&d, tda_fprint_i32); // [3, 4, 5]
    /// [build]

    /// [ends]
    // both ends are O(1) amortized, which a vec cannot do at the front. Any push may grow
    // the block, so every push returns a status
    int rc = 1;

    if (TDA_STATUS_IS_ERR(TDA_DEQUE_PUSH_FRONT(int32_t, &d, 2))) {
        goto out;
    }
    if (TDA_STATUS_IS_ERR(TDA_DEQUE_PUSH_BACK(int32_t, &d, 6))) {
        goto out;
    }
    tda_deque_print(&d, tda_fprint_i32); // [2, 3, 4, 5, 6]

    printf(
        "%" PRId32 " .. %" PRId32 "\n",
        *TDA_DEQUE_FRONT_AS(int32_t, &d),
        *TDA_DEQUE_BACK_AS(int32_t, &d)
    ); // 2 .. 6

    // pops take from either end and leave the capacity alone
    tda_deque_pop_front(&d);
    tda_deque_pop_back(&d);
    tda_deque_print(&d, tda_fprint_i32); // [3, 4, 5]
    /// [ends]

    /// [index]
    // an index counts from the front, so 0 is the front elem wherever the ring starts.
    // The seam stays hidden: the capacity says how much room there is, not where it wraps
    printf(
        "%" PRId32 " of %zu in room for %zu\n",
        *TDA_DEQUE_GET_AS(int32_t, &d, 0),
        tda_deque_len(&d),
        tda_deque_cap(&d)
    ); // 3 of 3 in room for 6

    TDA_DEQUE_SET(int32_t, &d, 1, 9);
    tda_deque_print(&d, tda_fprint_i32); // [3, 9, 5]

    // insert and remove are not what this type is for: they shift the shorter side, half
    // a vec's cost but still O(n)
    if (TDA_STATUS_IS_ERR(TDA_DEQUE_INSERT(int32_t, &d, 1, 7))) {
        goto out;
    }
    tda_deque_print(&d, tda_fprint_i32); // [3, 7, 9, 5]

    tda_deque_remove(&d, 1);
    tda_deque_print(&d, tda_fprint_i32); // [3, 9, 5]

    // the walk goes by index, so the seam never shows; a to_span would have had to copy
    TDA_DEQUE_FOR_EACH_AS (int32_t, elem, &d) {
        printf("%" PRId32 " ", *elem); // 3 9 5
    }
    putchar('\n');
    /// [index]

    /// [algo]
    // an algo that reads run by run takes the ring as it lies: at most two views, the
    // front run and the back one, nothing copied
    tda_Span front, back;
    tda_deque_to_spans(&d, &front, &back);

    constexpr int32_t key = 9;
    const bool found = tda_span_contains(front, &key, tda_eq_i32, nullptr)
        || tda_span_contains(back, &key, tda_eq_i32, nullptr);
    printf("%d\n", found); // 1

    // an algo that needs one run, like sort, goes through copies: out into a buffer of
    // exactly the right length, sort, and back
    int32_t buf[8];
    const tda_SpanMut s = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, tda_deque_len(&d));

    tda_deque_copy_to_span(&d, s);
    tda_span_sort(s, tda_cmp_i32, nullptr);
    tda_deque_copy_from_span(&d, tda_span_mut_to_span(s));

    tda_deque_print(&d, tda_fprint_i32); // [3, 5, 9]

    rc = 0;
out:
    // deinit of an inert value is a no-op, so this exit is safe from anywhere above
    tda_deque_deinit(&d);

    return rc;
    /// [algo]
}
