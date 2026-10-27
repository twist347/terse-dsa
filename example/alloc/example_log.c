// for @snippet

#include "tda/alloc/default.h"
#include "tda/alloc/log.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [wrap]
    // allocators compose: the log forwards every call and prints a line for it, and takes
    // no memory of its own
    tda_Log log = tda_log_init(stdout, tda_al_default());

    // tda log: alloc_zeroed size = 16 align = 4 -> 0x...
    int32_t *nums = TDA_ALLOC_ZEROED(int32_t, tda_log_to_al(&log), 4);
    if (nums) {
        TDA_DEALLOC(int32_t, tda_log_to_al(&log), nums, 4); // tda log: dealloc 0x... size = 16 align = 4
    }

    // the wrapped allocator must outlive the log, which passes every call on to it
    tda_log_deinit(&log);
    /// [wrap]

    return nums ? 0 : 1;
}
