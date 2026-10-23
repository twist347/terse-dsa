// for @snippet

#include "tda/alloc/debug.h"
#include "tda/alloc/default.h"
#include "tda/ds/vec.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [leaks]
    // put it under the code being checked; misuse stops the program with a report
    tda_Debug debug;
    tda_debug_init(tda_al_default(), &debug);
    tda_Al *al = TDA_AL(&debug);

    int32_t *kept = TDA_ALLOC(int32_t, al, 4);
    int32_t *dropped = TDA_ALLOC(int32_t, al, 8);
    TDA_DEALLOC(int32_t, al, dropped, 8);

    // tda debug: leak: block #1, 16 bytes at 0x...
    // tda debug: 1 leaked, 16 bytes
    const size_t leaked = tda_debug_fprint_leaks(&debug, stdout);

    // the serial in the report is where to stop next time, under a debugger:
    // tda_debug_break_at(&debug, 1);
    /// [leaks]

    TDA_DEALLOC(int32_t, al, kept, 4);

    /// [oom]
    // refuse the n-th request for each n in turn, until the operation needs no more: each
    // run checks one refusal path of the push
    for (size_t n = 0;; ++n) {
        tda_Vec v;
        tda_vec_init(TDA_LAYOUT_OF(int32_t), al, &v);

        tda_debug_fail_after(&debug, n);
        tda_Status st = TDA_STATUS_OK;
        for (int32_t i = 0; i < 20 && st == TDA_STATUS_OK; ++i) {
            st = TDA_VEC_PUSH(int32_t, &v, i);
        }
        tda_debug_fail_after(&debug, SIZE_MAX);

        tda_vec_deinit(&v);
        if (st == TDA_STATUS_OK) {
            break;
        }
    }

    printf("live after the oom runs: %zu\n", tda_debug_stats(&debug).live_blocks); // 0
    /// [oom]

    tda_debug_deinit(&debug);

    return leaked == 1 ? 0 : 1;
}
