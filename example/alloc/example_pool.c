// for @snippet

#include "tda/alloc/default.h"
#include "tda/alloc/pool.h"

#include <stdint.h>
#include <stdio.h>

int main() {
    /// [build]
    tda_Al *pool = tda_al_pool_new(tda_al_default(), TDA_LAYOUT_OF(int32_t), 3);
    if (!pool) {
        return 1;
    }

    // the block asked for is a minimum: it is raised to what the free list needs, a
    // pointer; the stats report the real one
    const tda_AlPoolStats st = tda_al_pool_stats(pool);
    printf("%zu blocks of %zu bytes at %zu, %zu free\n", st.block_count, st.block_size,
           st.block_align, st.free); // 3 blocks of 8 bytes at 8, 3 free — asked for 4 at 4
    /// [build]

    /// [limits]
    // every block has the same size, so a request is one pointer move or nothing
    void *p = nullptr;
    for (size_t i = 0; i < st.block_count; ++i) {
        p = tda_alloc(pool, TDA_LAYOUT_OF(int32_t));
    }
    printf("%zu free after taking them all\n", tda_al_pool_stats(pool).free); // 0

    // null now means the blocks ran out
    printf("%s\n", tda_alloc(pool, TDA_LAYOUT_OF(int32_t)) ? "served" : "null: none left");

    // null also comes back for a request larger than a block, even with blocks free
    tda_dealloc(pool, p, TDA_LAYOUT_OF(int32_t));
    const tda_Layout too_big = {.size = st.block_size + 1, .align = 1};
    printf("%s\n", tda_alloc(pool, too_big) ? "served" : "null: over a block");
    /// [limits]

    tda_al_pool_drop(pool);

    return 0;
}
