// for @snippet

#include "tda/alloc/debug.h"
#include "tda/alloc/default.h"
#include "tda/ds/vec.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [leaks]
    // put it under the code being checked; misuse stops the program with a report
    tda_Al *al = tda_al_debug_new(tda_al_default());
    if (!al) {
        return 1;
    }

    int32_t *kept = TDA_ALLOC(int32_t, al, 4);
    int32_t *dropped = TDA_ALLOC(int32_t, al, 8);
    TDA_DEALLOC(int32_t, al, dropped, 8);

    // tda debug: leak: block #1, 16 bytes at 0x...
    // tda debug: 1 leaked, 16 bytes
    const size_t leaked = tda_al_debug_fprint_leaks(al, stdout);

    // the serial in the report is where to stop next time, under a debugger:
    // tda_al_debug_break_at(al, 1);
    /// [leaks]

    TDA_DEALLOC(int32_t, al, kept, 4);

    /// [oom]
    // refuse the n-th request for each n in turn, until the operation needs no more: each
    // run checks one refusal path of the push
    for (size_t n = 0;; ++n) {
        tda_Vec *v;
        if (tda_vec_new(sizeof(int32_t), al, &v) != TDA_STATUS_OK) {
            return 1;
        }

        tda_al_debug_fail_after(al, n);
        tda_Status st = TDA_STATUS_OK;
        for (int32_t i = 0; i < 20 && st == TDA_STATUS_OK; ++i) {
            st = TDA_VEC_PUSH(int32_t, v, i);
        }
        tda_al_debug_fail_after(al, SIZE_MAX);

        tda_vec_drop(v);
        if (st == TDA_STATUS_OK) {
            break;
        }
    }

    printf("live after the oom runs: %zu\n", tda_al_debug_stats(al).live_blocks); // 0
    /// [oom]

    tda_al_debug_drop(al);

    return leaked == 1 ? 0 : 1;
}
