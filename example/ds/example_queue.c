// for @snippet

#include "tda/algo/search.h"
#include "tda/alloc/default.h"
#include "tda/core/cmp.h"
#include "tda/core/print.h"
#include "tda/core/span.h"
#include "tda/ds/deque.h"
#include "tda/ds/queue.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    // elems join in the order given, so the first in is the first served
    tda_Al *al = tda_al_default();

    tda_Queue q;
    if (TDA_STATUS_IS_ERR(TDA_QUEUE_OF(int32_t, al, &q, 1, 2, 3))) {
        return 1;
    }

    tda_queue_print(&q, tda_fprint_i32); // [1, 2, 3]
    printf(
        "%" PRId32 " .. %" PRId32 "\n",
        *TDA_QUEUE_FRONT_AS(int32_t, &q),
        *TDA_QUEUE_BACK_AS(int32_t, &q)
    ); // 1 .. 3
    /// [build]

    /// [fifo]
    // two ends, one job each: elems join at the back and leave from the front, both O(1)
    // amortized. There is no indexed get, insert, remove, push_front or pop_back, though
    // the deque underneath has them all
    int rc = 1;
    tda_Deque d = {}; // taken from the queue below; declared here for the exit

    if (TDA_STATUS_IS_ERR(TDA_QUEUE_PUSH(int32_t, &q, 4))) {
        goto out;
    }
    tda_queue_print(&q, tda_fprint_i32); // [1, 2, 3, 4]

    // both ends are writable; a queue limits where elems go in and out, not their values
    *TDA_QUEUE_FRONT_MUT_AS(int32_t, &q) = 9;

    // read the elem before popping: a pop that returned it would have nowhere to put the
    // bytes
    printf("%" PRId32 " ", *TDA_QUEUE_FRONT_AS(int32_t, &q));
    tda_queue_pop(&q);
    printf("%" PRId32 "\n", *TDA_QUEUE_FRONT_AS(int32_t, &q)); // 9 2
    /// [fifo]

    /// [read]
    // the elems may wrap the ring, so there is no view to hand over; the only bridge to
    // algo is a copy into a buffer exactly as long as the queue
    int32_t buf[8];
    const tda_SpanMut dst = TDA_SPAN_FROM_DATA_MUT(int32_t, buf, tda_queue_len(&q));

    tda_queue_copy_to_span(&q, dst);
    const tda_Span view = tda_span_mut_to_span(dst);

    printf(
        "%zu waiting, %zu of them 3\n",
        view.len,
        tda_span_count(view, &(int32_t){3}, tda_eq_i32, nullptr)
    ); // 3 waiting, 1 of them 3

    // and it is one way: sorting the copy says nothing about the queue, so there is no
    // copy_from_span to write it back
    tda_queue_print(&q, tda_fprint_i32); // [2, 3, 4]
    /// [read]

    /// [into]
    // the deque was under the queue all along, so taking it is free, and the queue is
    // left inert
    tda_queue_into_deque(&q, &d);

    // the narrow interface is gone too: the deque allows the front push a queue refused
    if (TDA_STATUS_IS_ERR(TDA_DEQUE_PUSH_FRONT(int32_t, &d, 1))) {
        goto out;
    }
    tda_deque_print(&d, tda_fprint_i32); // [1, 2, 3, 4]

    rc = 0;
out:
    // deinit of an inert value is a no-op, so this releases whichever one holds the elems
    tda_deque_deinit(&d);
    tda_queue_deinit(&q);

    return rc;
    /// [into]
}
