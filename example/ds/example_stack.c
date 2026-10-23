// for @snippet

#include "tda/algo/search.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/ds/stack.h"
#include "tda/ds/vec.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    // pushed in the order given: the FIRST ends up at the bottom, the last on top
    tda_Al *al = tda_al_default();

    tda_Stack *s = nullptr;
    if (TDA_STATUS_IS_ERR(TDA_STACK_OF(int32_t, al, &s, 1, 2, 3))) {
        return 1;
    }

    tda_stack_print(s, tda_fprint_i32); // [1, 2, 3]
    printf("%" PRId32 "\n", *TDA_STACK_TOP_AS(int32_t, s)); // 3
    /// [build]

    /// [lifo]
    // one end only: no indexed get, no insert, no remove, no way to reach the bottom. The
    // vec underneath could do all of it
    int rc = 1;
    tda_Vec *v = nullptr; // taken from the stack below; declared here for the exit

    if (TDA_STATUS_IS_ERR(TDA_STACK_PUSH(int32_t, s, 4))) {
        goto out;
    }
    printf("%" PRId32 "\n", *TDA_STACK_TOP_AS(int32_t, s)); // 4

    // the top is writable; a stack limits where elems go in and out, not their values
    *TDA_STACK_TOP_MUT_AS(int32_t, s) = 9;
    tda_stack_print(s, tda_fprint_i32); // [1, 2, 3, 9]

    // read the elem before popping: a pop that returned it would have nowhere to put the
    // bytes
    while (tda_stack_len(s) > 0) {
        printf("%" PRId32 " ", *TDA_STACK_TOP_AS(int32_t, s));
        tda_stack_pop(s);
    }
    putchar('\n'); // 9 3 2 1
    /// [lifo]

    /// [read]
    // the stack keeps its arrival order, so the bridge to algo is read only: a view to
    // search, count or fold, but none to sort through
    if (TDA_STATUS_IS_ERR(TDA_STACK_PUSH(int32_t, s, 7))) {
        goto out;
    }
    if (TDA_STATUS_IS_ERR(TDA_STACK_PUSH(int32_t, s, 8))) {
        goto out;
    }

    const tda_Span view = tda_stack_to_span(s); // bottom to top, so the top is LAST

    size_t idx;
    if (tda_span_find(view, &(int32_t){8}, tda_eq_i32, nullptr, &idx)) {
        printf("8 sits %zu from the bottom of %zu\n", idx, view.len); // 8 sits 1 from the bottom of 2
    }

    // the view borrows the buffer, so the next push may leave it dangling
    if (TDA_STATUS_IS_ERR(TDA_STACK_PUSH(int32_t, s, 5))) {
        goto out;
    }
    tda_stack_print(s, tda_fprint_i32); // [7, 8, 5]
    /// [read]

    /// [into]
    // the vec was under the stack all along, so taking it is free: same block, capacity
    // and allocator, nothing copied. It CONSUMES the stack: the header is freed and the
    // handle must not be used again
    v = tda_stack_into_vec(s);
    s = nullptr;

    // the narrow interface is gone too: the vec allows the indexing the stack refused
    tda_vec_print(v, tda_fprint_i32); // [7, 8, 5]
    printf("%" PRId32 "\n", *TDA_VEC_GET_AS(int32_t, v, 0)); // 7

    rc = 0;
out:
    // a null handle is a no-op, so this drops whichever of the two is still alive
    tda_vec_drop(v);
    tda_stack_drop(s);

    return rc;
    /// [into]
}
