// for @snippet

#include "tda/alloc/default.h"
#include "tda/alloc/log.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [wrap]
    // allocators compose: the log forwards every call and prints a line for it, and takes
    // no memory of its own
    tda_Log log;
    tda_log_init(tda_al_default(), stdout, &log);

    int32_t *nums = TDA_ALLOC_ZEROED(int32_t, TDA_AL(&log), 4); // [TDA] alloc_zeroed size = 16 align = 4 -> 0x...
    if (nums) {
        TDA_DEALLOC(int32_t, TDA_AL(&log), nums, 4); // [TDA] dealloc 0x... size = 16 align = 4
    }

    // the wrapped allocator must outlive the log, which passes every call on to it
    tda_log_deinit(&log);
    /// [wrap]

    return nums ? 0 : 1;
}
