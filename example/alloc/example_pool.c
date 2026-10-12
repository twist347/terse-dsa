// for @snippet

#include "tda/alloc/default.h"
#include "tda/alloc/pool.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    tda_Al *pool = tda_al_pool_new(tda_al_default(), sizeof(int32_t), 3);
    if (!pool) {
        return 1;
    }

    // the requested block size is a minimum: it is rounded up to the alignment and to
    // what the free list needs; the stats report the real size
    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    printf("%zu blocks of %zu bytes, %zu free\n", st.block_count, st.block_size,
           st.free); // 3 blocks of 16 bytes, 3 free — asked for 4
    /// [build]

    /// [limits]
    // every block has the same size, so a request is one pointer move or nothing
    void *p = nullptr;
    for (size_t i = 0; i < st.block_count; ++i) {
        p = tda_alloc(pool, sizeof(int32_t));
    }
    printf("%zu free after taking them all\n", tda_al_pool_stats(pool).free); // 0

    // null now means the blocks ran out
    printf("%s\n", tda_alloc(pool, sizeof(int32_t)) ? "served" : "null: none left");

    // null also comes back for a request larger than a block, even with blocks free
    tda_dealloc(pool, p, sizeof(int32_t));
    printf("%s\n", tda_alloc(pool, st.block_size + 1) ? "served" : "null: over a block");
    /// [limits]

    tda_al_pool_drop(pool);

    return 0;
}
