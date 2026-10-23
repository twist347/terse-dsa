// for @snippet

#include "tda/alloc/default.h"
#include "tda/alloc/log.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [wrap]
    // allocators compose: the log forwards every call and prints a line for it; its own
    // blocks come from the allocator it wraps
    tda_Al *log = tda_al_log_new(tda_al_default(), stdout);
    if (!log) {
        return 1;
    }

    int32_t *xs = TDA_ALLOC_ZEROED(int32_t, log, 4); // [TDA] alloc_zeroed size = 16 align = 4 -> 0x...
    if (xs) {
        TDA_DEALLOC(int32_t, log, xs, 4);            // [TDA] dealloc 0x... size = 16 align = 4
    }

    // the wrapped allocator must outlive the log, since the drop gives the log's blocks
    // back to it
    tda_al_log_drop(log);
    /// [wrap]

    return xs ? 0 : 1;
}
